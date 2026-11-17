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

#include "fellow_cell_proxies.h"
#include "fellow_terrain_3d.h"

#include "core/config/engine.h"
#include "core/io/dir_access.h"
#include "core/io/resource_loader.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/sort_array.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "servers/rendering/rendering_server.h"

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

// Milestone 66 (decision 0174): a cell's baked proxy, and a stand-in's full mesh's path.
static const char *FELLOW_CELL_PROXY_META = "_fellow_cell_proxy";
static const char *FELLOW_FULL_MESH_META = "_fellow_full_mesh";
static const char *FELLOW_MIDDLE_MESH_META = "_fellow_middle_mesh"; // decision 0266: the middle level's file
// Decision 0252: on a group's proxy, its cells (paths from it); on a cell, its group's proxy (a path from the cell).
static const char *FELLOW_GROUP_PROXY_META = "_fellow_group_proxy";
static const char *FELLOW_CELL_GROUP_META = "_fellow_group";
// Decision 0262: on a group's proxy, the coarser level's proxy over it (a path from it); on that one, the groups'
// proxies below it (paths from it).
static const char *FELLOW_GROUP_PARENT_META = "_fellow_group_parent";
static const char *FELLOW_GROUP_CHILDREN_META = "_fellow_group_children";

// A node's transform in an ancestor's space (in the tree or not).
static Transform3D _fellow_relative_xform(const Node *p_ancestor, const Node *p_node) {
	Transform3D x;
	for (const Node *n = p_node; n && n != p_ancestor; n = n->get_parent()) {
		const Node3D *n3 = Object::cast_to<Node3D>(n);
		if (n3) {
			x = n3->get_transform() * x;
		}
	}
	return x;
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
		_editor_hide_proxies();
		return;
	}
	if (editor_preview) {
		remove_child(editor_preview);
		memdelete(editor_preview);
		editor_preview = nullptr;
	}
	if (!show) {
		_editor_hide_proxies();
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
	_editor_hide_proxies();
#endif
}

void FellowStreamingCell3D::_editor_hide_proxies() {
#ifdef TOOLS_ENABLED
	// The preview in place of the proxies: their instances hidden, their saved visibility untouched.
	for (int i = 0; i < get_child_count(); i++) {
		GeometryInstance3D *g = Object::cast_to<GeometryInstance3D>(get_child(i));
		if (g && g->has_meta(FELLOW_CELL_PROXY_META) && g->get_instance().is_valid()) {
			RS::get_singleton()->instance_set_visible(g->get_instance(), !editor_preview && g->is_visible_in_tree());
		}
	}
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

// A cell's content made ready on the worker thread that instanced it, before it enters the tree (decision 0173): a
// terrain region builds its data (heights, mips, normals, splat maps, textures) here, not on the main thread.
static void _fellow_prepare_content(Node *p_node) {
	if (p_node == nullptr) {
		return;
	}
	LocalVector<Node *> stack;
	stack.push_back(p_node);
	while (!stack.is_empty()) {
		Node *n = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		if (FellowTerrain3D *t = Object::cast_to<FellowTerrain3D>(n)) {
			t->prepare_data();
		}
		for (int i = 0; i < n->get_child_count(); i++) {
			stack.push_back(n->get_child(i));
		}
	}
}

void FellowWorldStreamer3D::_instance_task(void *p_data) {
	Instancing *inst = static_cast<Instancing *>(p_data);
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	inst->result = inst->scene->instantiate();
	_fellow_prepare_content(inst->result);
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
	// Decision 0258: the content fading in over the proxy, which stays until it is whole (a ghost of this cell's earlier
	// content still fading out is dropped: the new content takes the share it had, the proxy keeps its own).
	float fade_from = -1.0f;
	if (crossfade > 0.0 && !Engine::get_singleton()->is_editor_hint() && is_inside_tree()) {
		fade_from = 0.0f;
		for (uint32_t i = 0; i < cell_fades.size(); i++) {
			if (cell_fades[i].cell == p_cell->get_instance_id()) {
				Node *ghost = ObjectDB::get_instance<Node>(cell_fades[i].ghost);
				if (ghost) {
					ghost->queue_free();
				}
				fade_from = cell_fades[i].in ? cell_fades[i].t : 1.0f - cell_fades[i].t;
				cell_fades.remove_at_unordered(i);
				break;
			}
		}
		CellFade f;
		f.cell = p_cell->get_instance_id();
		f.t = fade_from;
		f.in = true;
		cell_fades.push_back(f);
		_dither_tree(p_content, fade_from, false);
		_dither_proxies(p_cell, 1.0f - fade_from, true);
	} else {
		p_cell->_set_proxy_visible(false);
	}
	p_cell->state = FellowStreamingCell3D::STATE_LOADED;
	_update_group(p_cell);
	_register_meshes(p_cell, p_content);
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
	_unregister_meshes(p_cell);
	Node *c = p_cell->content;
	p_cell->content = nullptr;
	if (c && crossfade > 0.0 && !Engine::get_singleton()->is_editor_hint() && is_inside_tree() && c->get_parent() == p_cell) {
		// Decision 0258: the content kept as a ghost fading out while the proxy fades in (freed when done); a cell still
		// fading in turns back from where it is.
		float t = 0.0f;
		for (uint32_t i = 0; i < cell_fades.size(); i++) {
			if (cell_fades[i].cell == p_cell->get_instance_id()) {
				t = cell_fades[i].in ? 1.0f - cell_fades[i].t : cell_fades[i].t;
				cell_fades.remove_at_unordered(i);
				break;
			}
		}
		CellFade f;
		f.cell = p_cell->get_instance_id();
		f.ghost = c->get_instance_id();
		f.t = t;
		f.in = false;
		cell_fades.push_back(f);
		p_cell->_set_proxy_visible(true);
		_dither_proxies(p_cell, t, false);
		_dither_tree(c, 1.0f - t, true);
	} else {
		if (c) {
			if (c->get_parent() == p_cell) {
				p_cell->remove_child(c);
			}
			c->queue_free();
		}
		p_cell->_set_proxy_visible(true);
	}
	const bool was_loaded = p_cell->state == FellowStreamingCell3D::STATE_LOADED;
	p_cell->state = FellowStreamingCell3D::STATE_UNLOADED;
	_update_group(p_cell);
	const uint64_t dt = OS::get_singleton()->get_ticks_usec() - t0;
	release_usec += dt;
	release_usec_max = MAX(release_usec_max, dt);
	if (was_loaded) {
		releases++;
		p_cell->emit_signal(SNAME("unloaded"));
	}
}

// Decision 0258: every geometry instance below p_root keeps p_alpha of its pixels (the other share with p_other_share).
void FellowWorldStreamer3D::_dither_tree(Node *p_root, float p_alpha, bool p_other_share) {
	RenderingServer *rs = RenderingServer::get_singleton();
	LocalVector<Node *> stack;
	stack.push_back(p_root);
	while (!stack.is_empty()) {
		Node *n = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(n);
		if (gi && gi->get_instance().is_valid()) {
			rs->fellow_instance_set_dither_alpha(gi->get_instance(), p_alpha, p_other_share);
		} else if (FellowTerrain3D *t = Object::cast_to<FellowTerrain3D>(n)) {
			t->fellow_set_dither_alpha(p_alpha, p_other_share); // its patches and foliage
		}
		for (int i = 0; i < n->get_child_count(); i++) {
			stack.push_back(n->get_child(i));
		}
	}
}

void FellowWorldStreamer3D::_dither_proxies(FellowStreamingCell3D *p_cell, float p_alpha, bool p_other_share) {
	for (int i = 0; i < p_cell->get_child_count(); i++) {
		Node *n = p_cell->get_child(i);
		if (n != p_cell->content && n->has_meta(FELLOW_CELL_PROXY_META)) {
			_dither_tree(n, p_alpha, p_other_share);
		}
	}
}

// Decision 0259: the outgoing mesh drawn by an instance of its own (the node's transform, materials, layers; no shadow:
// the node casts its new mesh's) keeping the other share while the node's new mesh's grows.
void FellowWorldStreamer3D::_mesh_crossfade(MeshInstance3D *p_mi, const Ref<Mesh> &p_outgoing) {
	if (crossfade <= 0.0 || Engine::get_singleton()->is_editor_hint() || p_outgoing.is_null() || !p_mi->is_inside_tree() || !p_mi->is_visible_in_tree()) {
		return;
	}
	for (const CellFade &cf : cell_fades) {
		const FellowStreamingCell3D *cell = ObjectDB::get_instance<FellowStreamingCell3D>(cf.cell);
		if (cell && cell->is_ancestor_of(p_mi)) {
			return; // its cell crossing over already (the content's share is the cell's)
		}
	}
	RenderingServer *rs = RenderingServer::get_singleton();
	for (uint32_t i = 0; i < mesh_fades.size(); i++) {
		if (mesh_fades[i].node == p_mi->get_instance_id()) {
			// Swapped back while crossing: the earlier outgoing instance dropped.
			rs->free_rid(mesh_fades[i].outgoing);
			mesh_fades.remove_at_unordered(i);
			break;
		}
	}
	MeshFade f;
	f.node = p_mi->get_instance_id();
	f.mesh = p_outgoing;
	f.outgoing = rs->instance_create2(p_outgoing->get_rid(), p_mi->get_world_3d()->get_scenario());
	rs->instance_set_transform(f.outgoing, p_mi->get_global_transform());
	rs->instance_set_layer_mask(f.outgoing, p_mi->get_layer_mask());
	// Its shadow as the node's: the two shadows dithered as the meshes are (decision 0260), they make one.
	rs->instance_geometry_set_cast_shadows_setting(f.outgoing, RSE::ShadowCastingSetting(p_mi->get_cast_shadows_setting()));
	if (p_mi->get_material_override().is_valid()) {
		rs->instance_geometry_set_material_override(f.outgoing, p_mi->get_material_override()->get_rid());
	}
	for (int s = 0; s < p_mi->get_surface_override_material_count() && s < p_outgoing->get_surface_count(); s++) {
		const Ref<Material> m = p_mi->get_surface_override_material(s);
		if (m.is_valid()) {
			rs->instance_set_surface_override_material(f.outgoing, s, m->get_rid());
		}
	}
	rs->fellow_instance_set_dither_alpha(f.outgoing, 1.0f, true);
	rs->fellow_instance_set_dither_alpha(p_mi->get_instance(), 0.0f, false);
	mesh_fades.push_back(f);
}

void FellowWorldStreamer3D::_update_fades(double p_delta) {
	RenderingServer *rs = RenderingServer::get_singleton();
	for (uint32_t i = 0; i < mesh_fades.size();) {
		MeshFade &f = mesh_fades[i];
		MeshInstance3D *mi = ObjectDB::get_instance<MeshInstance3D>(f.node);
		f.t = MIN(1.0f, f.t + float(p_delta / crossfade));
		const bool done = f.t >= 1.0f || !mi || !mi->is_inside_tree();
		if (mi) {
			rs->fellow_instance_set_dither_alpha(mi->get_instance(), done ? 1.0f : f.t, false);
		}
		if (done) {
			rs->free_rid(f.outgoing);
			mesh_fades.remove_at_unordered(i);
		} else {
			rs->fellow_instance_set_dither_alpha(f.outgoing, 1.0f - f.t, true);
			i++;
		}
	}
	for (uint32_t i = 0; i < cell_fades.size();) {
		CellFade &f = cell_fades[i];
		FellowStreamingCell3D *cell = ObjectDB::get_instance<FellowStreamingCell3D>(f.cell);
		Node *ghost = ObjectDB::get_instance<Node>(f.ghost);
		f.t = MIN(1.0f, f.t + float(p_delta / crossfade));
		const bool done = f.t >= 1.0f || !cell || (!f.in && !ghost) || (f.in && !cell->content);
		if (f.in) {
			if (cell && cell->content) {
				_dither_tree(cell->content, done ? 1.0f : f.t, false);
			}
			if (cell) {
				if (done) {
					_dither_proxies(cell, 1.0f, false);
					cell->_set_proxy_visible(cell->state != FellowStreamingCell3D::STATE_LOADED);
				} else {
					_dither_proxies(cell, 1.0f - f.t, true);
				}
			}
		} else {
			if (cell) {
				_dither_proxies(cell, done ? 1.0f : f.t, false);
			}
			if (ghost) {
				if (done) {
					ghost->queue_free();
				} else {
					_dither_tree(ghost, 1.0f - f.t, true);
				}
			}
		}
		if (done) {
			cell_fades.remove_at_unordered(i);
		} else {
			i++;
		}
	}
}

void FellowWorldStreamer3D::_clear_fades() {
	for (const MeshFade &f : mesh_fades) {
		RenderingServer::get_singleton()->free_rid(f.outgoing);
		MeshInstance3D *mi = ObjectDB::get_instance<MeshInstance3D>(f.node);
		if (mi) {
			RenderingServer::get_singleton()->fellow_instance_set_dither_alpha(mi->get_instance(), 1.0f, false);
		}
	}
	mesh_fades.clear();
	for (const CellFade &f : cell_fades) {
		Node *ghost = ObjectDB::get_instance<Node>(f.ghost);
		if (ghost) {
			ghost->queue_free();
		}
		FellowStreamingCell3D *cell = ObjectDB::get_instance<FellowStreamingCell3D>(f.cell);
		if (cell) {
			_dither_proxies(cell, 1.0f, false);
			if (cell->content) {
				_dither_tree(cell->content, 1.0f, false);
			}
			cell->_set_proxy_visible(cell->state != FellowStreamingCell3D::STATE_LOADED);
		}
	}
	cell_fades.clear();
}

// Decision 0252: a group's proxy is drawn (beyond its distance) while none of its cells is loaded; hidden, it leaves
// its cells' own proxies to the visibility ranges (a hidden visibility parent does not hide its dependants).
void FellowWorldStreamer3D::_update_group(FellowStreamingCell3D *p_cell) {
	if (!p_cell->has_meta(FELLOW_CELL_GROUP_META)) {
		return;
	}
	GeometryInstance3D *group = Object::cast_to<GeometryInstance3D>(p_cell->get_node_or_null(NodePath(p_cell->get_meta(FELLOW_CELL_GROUP_META))));
	if (!group) {
		return;
	}
	const Array paths = group->get_meta(FELLOW_GROUP_PROXY_META, Array());
	bool loaded = false;
	for (int i = 0; i < paths.size() && !loaded; i++) {
		const FellowStreamingCell3D *c = Object::cast_to<FellowStreamingCell3D>(group->get_node_or_null(NodePath(paths[i])));
		loaded = c && c->state == FellowStreamingCell3D::STATE_LOADED;
	}
	group->set_visible(!loaded);
	// Decision 0262: a coarser level's proxy drawn while all the groups' proxies below it are (none of its cells
	// loaded); hidden, it leaves them to their visibility ranges.
	GeometryInstance3D *g = group;
	while (g->has_meta(FELLOW_GROUP_PARENT_META)) {
		GeometryInstance3D *parent = Object::cast_to<GeometryInstance3D>(g->get_node_or_null(NodePath(g->get_meta(FELLOW_GROUP_PARENT_META))));
		if (!parent) {
			break;
		}
		const Array children = parent->get_meta(FELLOW_GROUP_CHILDREN_META, Array());
		bool all = true;
		for (int i = 0; i < children.size() && all; i++) {
			const Node3D *child = Object::cast_to<Node3D>(parent->get_node_or_null(NodePath(children[i])));
			all = !child || child->is_visible();
		}
		parent->set_visible(all);
		g = parent;
	}
}

void FellowWorldStreamer3D::_editor_hide_groups() {
#ifdef TOOLS_ENABLED
	// As the cells' proxies under the editor's preview: the instances hidden, the saved visibility untouched.
	if (!Engine::get_singleton()->is_editor_hint()) {
		return;
	}
	for (int i = 0; i < get_child_count(); i++) {
		GeometryInstance3D *g = Object::cast_to<GeometryInstance3D>(get_child(i));
		if (g && g->has_meta(FELLOW_GROUP_PROXY_META) && g->get_instance().is_valid()) {
			RS::get_singleton()->instance_set_visible(g->get_instance(), !editor_preview && g->is_visible_in_tree());
		}
	}
#endif
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

	uint64_t t_step = OS::get_singleton()->get_ticks_usec();
	update_instanced_usec += t_step - t0;

	// 2. Distances.
	LocalVector<Vector3> points;
	_gather_points(points);
	_update_meshes(points);
	{
		const uint64_t t = OS::get_singleton()->get_ticks_usec();
		update_meshes_usec += t - t_step;
		t_step = t;
	}
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

	{
		const uint64_t t = OS::get_singleton()->get_ticks_usec();
		update_loaded_usec += t - t_step; // with the distances
		t_step = t;
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
	const uint64_t t_requests = OS::get_singleton()->get_ticks_usec();
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
	const uint64_t t_end = OS::get_singleton()->get_ticks_usec();
	update_requests_usec += t_end - t_requests;
	update_usec += t_end - t0;
	update_usec_max = MAX(update_usec_max, t_end - t0);
}

// ---------------------------------------------------------------------------------------------------------------------
// Streamed meshes and cell proxies (decision 0174)

void FellowWorldStreamer3D::_register_meshes(FellowStreamingCell3D *p_cell, Node *p_content) {
	LocalVector<Node *> stack;
	stack.push_back(p_content);
	while (!stack.is_empty()) {
		Node *n = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n)) {
			if (mi->has_meta(FELLOW_FULL_MESH_META)) {
				StreamedMesh m;
				m.node = mi->get_instance_id();
				m.cell = p_cell;
				m.path = mi->get_meta(FELLOW_FULL_MESH_META);
				m.middle_path = mi->get_meta(FELLOW_MIDDLE_MESH_META, String());
				m.stand_in = mi->get_mesh();
				streamed_meshes.push_back(m);
			}
		}
		for (int i = 0; i < n->get_child_count(); i++) {
			stack.push_back(n->get_child(i));
		}
	}
}

void FellowWorldStreamer3D::_unregister_meshes(FellowStreamingCell3D *p_cell) {
	for (uint32_t i = 0; i < streamed_meshes.size();) {
		if (streamed_meshes[i].cell == p_cell) {
			streamed_meshes.remove_at_unordered(i); // a load still pending is collected (and dropped) when it ends
		} else {
			i++;
		}
	}
}

void FellowWorldStreamer3D::_update_meshes(const LocalVector<Vector3> &p_points) {
	// Loads that ended: their meshes onto the instances still waiting for them.
	LocalVector<String> done;
	for (const String &path : mesh_pending) {
		if (ResourceLoader::load_threaded_get_status(path) != ResourceLoader::THREAD_LOAD_IN_PROGRESS) {
			done.push_back(path);
		}
	}
	for (const String &path : done) {
		mesh_pending.erase(path);
		const Ref<Mesh> mesh = ResourceLoader::load_threaded_get(path);
		for (StreamedMesh &m : streamed_meshes) {
			if (m.loading < 0 || (m.loading == 2 ? m.path : m.middle_path) != path) {
				continue;
			}
			MeshInstance3D *mi = ObjectDB::get_instance<MeshInstance3D>(m.node);
			const int level = m.loading;
			m.loading = -1;
			if (mesh.is_null() || !mi) {
				m.failed = true;
				continue;
			}
			const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
			const Ref<Mesh> outgoing = mi->get_mesh();
			mi->set_mesh(mesh);
			_mesh_crossfade(mi, outgoing); // decision 0259
			const uint64_t dt = OS::get_singleton()->get_ticks_usec() - t0;
			mesh_swap_usec += dt;
			mesh_swap_usec_max = MAX(mesh_swap_usec_max, dt);
			m.loaded = mesh; // (the level shown before dropped)
			m.level = level;
			mesh_loads++;
		}
	}
	// Each mesh's level by its distance: the full mesh within mesh_distance, the middle level within
	// mesh_middle_distance (decision 0266), the stand-in beyond; a coarser level only past the distance and the unload
	// margin. A finer level is loaded (a few at a time) and swapped in when it arrives; a coarser one as well, but the
	// stand-in, in the cell's content, at once.
	for (StreamedMesh &m : streamed_meshes) {
		MeshInstance3D *mi = ObjectDB::get_instance<MeshInstance3D>(m.node);
		if (!mi || !mi->is_inside_tree() || m.failed) {
			continue;
		}
		int target = 2;
		if (mesh_distance > 0.0) {
			float d = 1e30f;
			const AABB box = mi->get_global_transform().xform(mi->get_aabb());
			for (const Vector3 &p : p_points) {
				d = MIN(d, _point_aabb_distance(p, box));
			}
			const bool middle = !m.middle_path.is_empty() && mesh_middle_distance > mesh_distance;
			if (d <= mesh_distance + (m.level >= 2 ? unload_margin : 0.0)) {
				target = 2;
			} else if (middle && d <= mesh_middle_distance + (m.level >= 1 ? unload_margin : 0.0)) {
				target = 1;
			} else {
				target = 0;
			}
		}
		if (target == m.level || m.loading == target) {
			continue;
		}
		if (target == 0) {
			if (m.loading >= 0) {
				continue; // (a load in flight: dropped when it lands on the wrong level, below)
			}
			const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
			const Ref<Mesh> outgoing = mi->get_mesh();
			mi->set_mesh(m.stand_in);
			_mesh_crossfade(mi, outgoing); // decision 0259
			m.loaded.unref();
			const uint64_t dt = OS::get_singleton()->get_ticks_usec() - t0;
			mesh_swap_usec += dt;
			mesh_swap_usec_max = MAX(mesh_swap_usec_max, dt);
			m.level = 0;
			mesh_releases++;
		} else if (m.loading < 0) {
			const String &path = target == 2 ? m.path : m.middle_path;
			if (mesh_pending.has(path)) {
				m.loading = target;
			} else if (int(mesh_pending.size()) < max_mesh_loads && ResourceLoader::load_threaded_request(path, "Mesh") == OK) {
				mesh_pending.insert(path);
				m.loading = target;
			}
		}
	}
}

Dictionary FellowWorldStreamer3D::bake_proxies(const String &p_directory, const Dictionary &p_options) {
	Dictionary info;
	const float ratio = p_options.get("ratio", 0.05f);
	const int terrain_quads = p_options.get("terrain_quads", 32);
	const bool stream_meshes = p_options.get("stream_meshes", false);
	const float mesh_ratio = p_options.get("mesh_ratio", 0.15f);
	// Decision 0266: a middle level of each streamed mesh, this share of its triangles (0: none), saved apart and loaded
	// within mesh_middle_distance; one per full mesh's file.
	const float mesh_middle_ratio = p_options.get("mesh_middle_ratio", 0.0f);
	HashMap<String, String> middle_of;
	int middle_meshes = 0;
	int64_t middle_triangles = 0;
	const int mesh_min_triangles = p_options.get("mesh_min_triangles", 2000);
	const real_t proxy_distance = p_options.get("proxy_distance", 0.0);
	const bool proxy_shadows = p_options.get("proxy_shadows", false);
	const float texel_size = p_options.get("texel_size", 0.5f);
	const int max_texture_size = p_options.get("max_texture_size", 512);
	const int group_cells = p_options.get("group_cells", 4);
	const real_t group_distance = p_options.get("group_distance", 0.0);
	// Decision 0256: the cells' proxies fade out over this distance on each side of their group's switch (0: a switch).
	const real_t group_fade = p_options.get("group_fade", 0.0);
	// Decision 0263: each textured proxy's texture in a file of its own beside it (<proxy>_albedo.ctex), streamed with
	// rendering/fellow/streaming/textures (false: kept in the proxy's file, loaded whole).
	const bool texture_files = p_options.get("texture_files", true);
	int texture_files_written = 0;
	int64_t texture_file_bytes = 0;
	auto texture_file = [&](const Ref<ArrayMesh> &p_mesh, const String &p_path) {
		int64_t bytes = 0;
		if (texture_files && FellowCellProxies::move_texture_to_file(p_mesh, p_path.get_basename() + "_albedo.ctex", bytes)) {
			texture_files_written++;
			texture_file_bytes += bytes;
		}
	};
	// Decision 0262: levels of groups — the groups' proxies in groups of group_cells x group_cells again, each with a
	// coarser proxy for farther away (1: the groups alone).
	const int group_levels = CLAMP(int(p_options.get("group_levels", 1)), 1, 4);
	// A terrain's proxies as far as an error of terrain_error pixels allows where they are first drawn, in a view of
	// 1 080 lines with Godot's default field of view (75 degrees): metres per metre of distance (decision 0255).
	const float terrain_error = p_options.get("terrain_error", 2.0f);
	const float error_per_metre = terrain_error * 2.0f * Math::tan(Math::deg_to_rad(37.5f)) / 1080.0f;
	Node *owner = Object::cast_to<Node>(p_options.get("owner", Variant()).get_validated_object());
	if (!owner) {
		owner = get_owner();
	}
	const Error dir_err = DirAccess::make_dir_recursive_absolute(stream_meshes ? p_directory.path_join("meshes") : p_directory);
	ERR_FAIL_COND_V_MSG(dir_err != OK && dir_err != ERR_ALREADY_EXISTS, info, vformat("Cannot create the directory \"%s\".", p_directory));
	// One material for the vertex-coloured proxies, saved beside them (a textured proxy has its own, in its file).
	Ref<StandardMaterial3D> material = FellowCellProxies::proxy_material()->duplicate();
	const String material_path = p_directory.path_join("fellow_proxy_material.tres");
	if (ResourceSaver::save(material, material_path) == OK) {
		material->set_path(material_path, true); // the proxies refer to the file (FLAG_CHANGE_PATH keeps no path)
	}
	int proxies = 0;
	int source_triangles = 0;
	int proxy_triangles = 0;
	int textured = 0;
	int64_t texture_bytes = 0;
	int64_t texture_usec = 0;
	int64_t texture_texels = 0;
	int64_t texture_hits = 0;
	int groups_baked = 0;
	int group_triangles = 0;
	// Decision 0262: the groups' proxies baked, for the coarser levels.
	struct GroupProxy {
		MeshInstance3D *node = nullptr;
		Vector2i key;
		AABB bounds; // its cells' boxes, in the streamer's space
		real_t begin = 0.0; // the distance it is drawn from (to its centre)
		float texel = 0.0f;
		int triangles = 0;
		bool terrain = false; // mostly a terrain's
		LocalVector<FellowStreamingCell3D *> cells;
	};
	LocalVector<GroupProxy> level_proxies;
	Array level_info;
	int terrain_triangles = 0; // the terrains' among the proxies' and the groups'
	int group_terrain_triangles = 0;
	int64_t group_texture_bytes = 0;
	int streamed = 0;
	int stream_before = 0;
	int stream_after = 0;
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	// The cells below this streamer (in the editor they are not registered with it).
	LocalVector<FellowStreamingCell3D *> targets;
	LocalVector<Node *> walk;
	walk.push_back(this);
	while (!walk.is_empty()) {
		Node *n = walk[walk.size() - 1];
		walk.resize(walk.size() - 1);
		for (int i = 0; i < n->get_child_count(); i++) {
			Node *c = n->get_child(i);
			FellowStreamingCell3D *cell = Object::cast_to<FellowStreamingCell3D>(c);
			if (cell) {
				if (cell->_find_streamer() == this) {
					targets.push_back(cell);
				}
			} else if (!Object::cast_to<FellowWorldStreamer3D>(c)) {
				walk.push_back(c);
			}
		}
	}
	// Decision 0252: the cells in groups of group_cells x group_cells, on a grid of their boxes' centres in the
	// streamer's space (the pitch: group_cells times the cells' median size), each group with one proxy for afar.
	for (FellowStreamingCell3D *cell : targets) {
		for (int i = 0; i < cell->get_child_count(); i++) {
			Node3D *proxy = Object::cast_to<Node3D>(cell->get_child(i));
			if (proxy && proxy->has_meta(FELLOW_CELL_PROXY_META)) {
				proxy->set_visibility_parent(NodePath()); // (before its group's proxy goes)
			}
		}
	}
	for (int i = get_child_count() - 1; i >= 0; i--) {
		Node3D *g = Object::cast_to<Node3D>(get_child(i));
		if (g && g->has_meta(FELLOW_GROUP_PARENT_META)) {
			g->set_visibility_parent(NodePath()); // (decision 0262: before the coarser level's proxy goes)
		}
	}
	for (int i = get_child_count() - 1; i >= 0; i--) {
		Node *c = get_child(i);
		if (c->has_meta(FELLOW_GROUP_PROXY_META)) {
			remove_child(c);
			memdelete(c);
		}
	}
	LocalVector<LocalVector<FellowStreamingCell3D *>> groups;
	LocalVector<Vector2i> group_keys;
	if (group_cells > 1 && targets.size() > 1) {
		LocalVector<real_t> sizes;
		real_t min_x = 1e30;
		real_t min_z = 1e30;
		for (FellowStreamingCell3D *cell : targets) {
			const AABB b = _fellow_relative_xform(this, cell).xform(cell->bounds);
			sizes.push_back(MAX(b.size.x, b.size.z));
			min_x = MIN(min_x, b.position.x);
			min_z = MIN(min_z, b.position.z);
		}
		sizes.sort();
		const real_t pitch = MAX(real_t(1.0), sizes[sizes.size() / 2] * group_cells);
		HashMap<Vector2i, int> index;
		for (FellowStreamingCell3D *cell : targets) {
			const Vector3 c = _fellow_relative_xform(this, cell).xform(cell->bounds).get_center();
			const Vector2i key(int(Math::floor((c.x - min_x) / pitch)), int(Math::floor((c.z - min_z) / pitch)));
			if (!index.has(key)) {
				index.insert(key, groups.size());
				groups.push_back(LocalVector<FellowStreamingCell3D *>());
				group_keys.push_back(key);
			}
			groups[index[key]].push_back(cell);
		}
	} else {
		for (FellowStreamingCell3D *cell : targets) {
			groups.push_back(LocalVector<FellowStreamingCell3D *>());
			groups[groups.size() - 1].push_back(cell);
			group_keys.push_back(Vector2i());
		}
	}
	for (FellowStreamingCell3D *cell : targets) {
		cell->remove_meta(FELLOW_CELL_GROUP_META);
	}
	for (uint32_t gi = 0; gi < groups.size(); gi++) {
		LocalVector<Node *> contents;
		LocalVector<FellowStreamingCell3D *> baked;
		int baked_triangles = 0; // the group's cells' proxies
		for (FellowStreamingCell3D *cell : groups[gi]) {
			if (cell->scene_path.is_empty()) {
				continue;
			}
			const Ref<PackedScene> ps = ResourceLoader::load(cell->scene_path, "PackedScene");
			Node *content = ps.is_valid() ? ps->instantiate() : nullptr;
			if (!content) {
				ERR_PRINT(vformat("FellowWorldStreamer3D: could not load '%s' to bake its proxy.", cell->scene_path));
				continue;
			}
			// The proxy, in place of an earlier one.
			for (int i = cell->get_child_count() - 1; i >= 0; i--) {
				Node *c = cell->get_child(i);
				if (c != cell->content && c->has_meta(FELLOW_CELL_PROXY_META)) {
					cell->remove_child(c);
					memdelete(c);
				}
			}
			Dictionary pi;
			const real_t first_drawn = (cell->load_distance > 0.0 ? cell->load_distance : load_distance) + unload_margin;
			Ref<ArrayMesh> proxy = FellowCellProxies::bake_proxy(content, ratio, terrain_quads, texel_size, max_texture_size, pi, 0, float(first_drawn) * error_per_metre);
			source_triangles += int(pi.get("source_triangles", 0));
			if (proxy.is_valid()) {
				if (pi.has("texture_width")) {
					textured++;
					texture_bytes += int64_t(pi.get("texture_bytes", 0));
					texture_usec += int64_t(pi.get("texture_usec", 0));
					texture_texels += int64_t(pi.get("texture_texels", 0));
					texture_hits += int64_t(pi.get("texture_hits", 0));
				} else {
					proxy->surface_set_material(0, material);
				}
				const String path = p_directory.path_join(String(cell->get_name()) + "_proxy.res");
				texture_file(proxy, path);
				if (ResourceSaver::save(proxy, path, ResourceSaver::FLAG_COMPRESS) == OK) {
					proxy->set_path(path, true); // the scene saved with the proxy refers to the file, not a copy
					MeshInstance3D *mi = memnew(MeshInstance3D);
					mi->set_name("FellowProxy");
					mi->set_meta(FELLOW_CELL_PROXY_META, true);
					mi->set_mesh(proxy);
					mi->set_cast_shadows_setting(proxy_shadows ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
					if (proxy_distance > 0.0) {
						mi->set_visibility_range_end(proxy_distance);
					}
					cell->add_child(mi);
					mi->set_visible(cell->state != FellowStreamingCell3D::STATE_LOADED);
					if (owner) {
						mi->set_owner(owner);
					}
					cell->_editor_hide_proxies();
					proxies++;
					proxy_triangles += int(pi.get("proxy_triangles", 0));
					terrain_triangles += int(pi.get("proxy_terrain_triangles", 0));
					baked_triangles += int(pi.get("proxy_triangles", 0)) - int(pi.get("proxy_terrain_triangles", 0)); // (a terrain's as its error allows)
				}
			}
			// The large meshes' stand-ins, their full meshes apart.
			if (stream_meshes) {
				bool changed = false;
				int index = 0;
				LocalVector<Node *> stack;
				stack.push_back(content);
				while (!stack.is_empty()) {
					Node *n = stack[stack.size() - 1];
					stack.resize(stack.size() - 1);
					for (int i = 0; i < n->get_child_count(); i++) {
						stack.push_back(n->get_child(i));
					}
					MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n);
					if (!mi || mi->get_mesh().is_null() || mi->get_skin().is_valid() || mi->has_meta(FELLOW_FULL_MESH_META)) {
						continue;
					}
					const Ref<Mesh> mesh = mi->get_mesh();
					const int tris = FellowCellProxies::mesh_triangles(mesh);
					if (tris < mesh_min_triangles) {
						continue;
					}
					const Ref<ArrayMesh> stand_in = FellowCellProxies::simplified_mesh(mesh, mesh_ratio);
					if (stand_in.is_null()) {
						continue;
					}
					String full_path = mesh->get_path();
					if (full_path.is_empty() || full_path.contains("::")) {
						// Built into the cell's scene: saved on its own (a copy: the cached scene keeps its own).
						const Ref<Mesh> copy = mesh->duplicate();
						full_path = p_directory.path_join("meshes").path_join(vformat("%s_%d.res", String(cell->get_name()), index++));
						if (ResourceSaver::save(copy, full_path, ResourceSaver::FLAG_COMPRESS) != OK) {
							continue;
						}
					}
					mi->set_mesh(stand_in);
					mi->set_meta(FELLOW_FULL_MESH_META, full_path);
					if (mesh_middle_ratio > mesh_ratio && mesh_middle_ratio < 1.0f) {
						String middle_path;
						if (const String *found = middle_of.getptr(full_path)) {
							middle_path = *found;
						} else {
							const Ref<ArrayMesh> middle = FellowCellProxies::simplified_mesh(mesh, mesh_middle_ratio);
							if (middle.is_valid()) {
								const String path = p_directory.path_join("meshes").path_join(vformat("%s_middle_%s.res", full_path.get_file().get_basename(), full_path.md5_text().substr(0, 8)));
								if (ResourceSaver::save(middle, path, ResourceSaver::FLAG_COMPRESS) == OK) {
									middle_path = path;
									middle_meshes++;
									middle_triangles += FellowCellProxies::mesh_triangles(middle);
								}
							}
							middle_of.insert(full_path, middle_path);
						}
						if (!middle_path.is_empty()) {
							mi->set_meta(FELLOW_MIDDLE_MESH_META, middle_path);
						}
					}
					changed = true;
					streamed++;
					stream_before += tris;
					stream_after += FellowCellProxies::mesh_triangles(stand_in);
				}
				if (changed) {
					Ref<PackedScene> scene;
					scene.instantiate();
					const String path = p_directory.path_join(String(cell->get_name()) + ".scn");
					if (scene->pack(content) == OK && ResourceSaver::save(scene, path) == OK) {
						cell->set_scene_path(path);
					}
				}
			}
			contents.push_back(content);
			baked.push_back(cell);
		}
		// The group's proxy: its cells' content together, simplified and textured for its distance (its cells'
		// proxies' triangles over the square of how much farther, their texel times it; a terrain's quads as its
		// cells', so that the edges it keeps meet its neighbours'), drawn beyond the distance at which none of its
		// cells can be loaded (the streamer's distance, its margin, and the farthest point of the cells' boxes from
		// the proxy's centre: visibility ranges measure to it); the cells' proxies hidden there (it is their
		// visibility parent).
		if (group_cells > 1 && baked.size() >= 2) {
			AABB bounds;
			real_t load = load_distance;
			Node3D *root = memnew(Node3D);
			for (uint32_t i = 0; i < baked.size(); i++) {
				const Transform3D x = _fellow_relative_xform(this, baked[i]);
				bounds = i == 0 ? x.xform(baked[i]->bounds) : bounds.merge(x.xform(baked[i]->bounds));
				load = MAX(load, baked[i]->load_distance);
			}
			const Vector3 origin = bounds.get_center();
			for (uint32_t i = 0; i < baked.size(); i++) {
				Node3D *wrap = memnew(Node3D);
				wrap->set_transform(Transform3D(Basis(), -origin) * _fellow_relative_xform(this, baked[i]));
				root->add_child(wrap);
				wrap->add_child(contents[i]);
			}
			contents.clear(); // (freed with the root)
			// (How much farther than its cells' proxies it is seen: at most as much as it is larger.)
			const float scale = CLAMP(float((load + unload_margin + bounds.size.length() * 0.5) / MAX(load, real_t(1.0))), 1.0f, float(group_cells));
			Dictionary gpi;
			const real_t group_drawn = group_distance > 0.0 ? group_distance : load + unload_margin + bounds.size.length() * 0.5;
			Ref<ArrayMesh> group = FellowCellProxies::bake_proxy(root, ratio, terrain_quads, texel_size > 0.0f ? texel_size * scale : 0.0f, max_texture_size, gpi, MAX(1, int(baked_triangles / (scale * scale))), float(group_drawn) * error_per_metre);
			memdelete(root);
			if (group.is_valid()) {
				if (gpi.has("texture_width")) {
					group_texture_bytes += int64_t(gpi.get("texture_bytes", 0));
				} else {
					group->surface_set_material(0, material);
				}
				const Vector2i key = group_keys[gi];
				const String path = p_directory.path_join(vformat("group_%d_%d_proxy.res", key.x, key.y));
				texture_file(group, path);
				if (ResourceSaver::save(group, path, ResourceSaver::FLAG_COMPRESS) == OK) {
					group->set_path(path, true);
					const Vector3 center = group->get_aabb().get_center();
					real_t reach = 0.0;
					for (int k = 0; k < 8; k++) {
						reach = MAX(reach, (bounds.get_endpoint(k) - origin).distance_to(center));
					}
					MeshInstance3D *gmi = memnew(MeshInstance3D);
					gmi->set_name(vformat("FellowGroupProxy_%d_%d", key.x, key.y));
					gmi->set_mesh(group);
					gmi->set_position(origin);
					gmi->set_cast_shadows_setting(proxy_shadows ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
					gmi->set_visibility_range_begin(group_distance > 0.0 ? group_distance : load + unload_margin + reach);
					if (group_fade > 0.0) {
						gmi->set_visibility_range_begin_margin(group_fade);
						gmi->set_visibility_range_fade_mode(GeometryInstance3D::VISIBILITY_RANGE_FADE_DEPENDENCIES);
					}
					if (proxy_distance > 0.0) {
						gmi->set_visibility_range_end(proxy_distance);
					}
					add_child(gmi);
					if (owner) {
						gmi->set_owner(owner);
					}
					Array paths;
					for (FellowStreamingCell3D *cell : baked) {
						paths.push_back(gmi->get_path_to(cell));
						cell->set_meta(FELLOW_CELL_GROUP_META, cell->get_path_to(gmi));
						for (int i = 0; i < cell->get_child_count(); i++) {
							Node3D *proxy = Object::cast_to<Node3D>(cell->get_child(i));
							if (proxy && proxy->has_meta(FELLOW_CELL_PROXY_META)) {
								proxy->set_visibility_parent(proxy->get_path_to(gmi));
							}
						}
					}
					gmi->set_meta(FELLOW_GROUP_PROXY_META, paths);
					_update_group(baked[0]);
					groups_baked++;
					group_triangles += int(gpi.get("proxy_triangles", 0));
					group_terrain_triangles += int(gpi.get("proxy_terrain_triangles", 0));
					GroupProxy gp;
					gp.node = gmi;
					gp.key = key;
					gp.bounds = bounds;
					gp.begin = gmi->get_visibility_range_begin();
					gp.texel = texel_size > 0.0f ? texel_size * scale : 0.0f;
					gp.triangles = int(gpi.get("proxy_triangles", 0));
					gp.terrain = int(gpi.get("proxy_terrain_triangles", 0)) * 2 >= gp.triangles;
					for (FellowStreamingCell3D *cell : baked) {
						gp.cells.push_back(cell);
					}
					level_proxies.push_back(gp);
				}
			}
		}
		for (Node *content : contents) {
			memdelete(content);
		}
	}
	// Decision 0262: the coarser levels, each from the level below's proxies (their meshes, textures and terrains:
	// cheap to bake, and the terrain's outer edges where the finer level has them, so that neighbours meet). A level's
	// proxy is drawn from where each of the proxies below it would be drawn (their distance plus how far their centre
	// is from its), simplified and textured for how much farther that is; it is their visibility parent.
	for (int level = 2; level <= group_levels && level_proxies.size() > 1 && group_cells > 1; level++) {
		HashMap<Vector2i, int> index;
		LocalVector<LocalVector<int>> sets;
		LocalVector<Vector2i> keys;
		for (uint32_t i = 0; i < level_proxies.size(); i++) {
			const Vector2i k = level_proxies[i].key;
			const Vector2i key(int(Math::floor(real_t(k.x) / group_cells)), int(Math::floor(real_t(k.y) / group_cells)));
			if (!index.has(key)) {
				index.insert(key, sets.size());
				sets.push_back(LocalVector<int>());
				keys.push_back(key);
			}
			sets[index[key]].push_back(i);
		}
		LocalVector<GroupProxy> next;
		int baked_level = 0;
		int level_triangles = 0;
		int64_t level_texture_bytes = 0;
		String texture_skipped;
		for (uint32_t si = 0; si < sets.size(); si++) {
			const LocalVector<int> &set = sets[si];
			if (set.size() < 2) {
				continue;
			}
			AABB bounds;
			int triangles = 0;
			int terrain_triangles_below = 0;
			float texel = 0.0f;
			for (uint32_t j = 0; j < set.size(); j++) {
				const GroupProxy &gp = level_proxies[set[j]];
				bounds = j == 0 ? gp.bounds : bounds.merge(gp.bounds);
				triangles += gp.triangles;
				terrain_triangles_below += gp.terrain ? gp.triangles : 0;
				texel = MAX(texel, gp.texel);
			}
			const Vector3 origin = bounds.get_center();
			real_t begin = 0.0;
			real_t begin_below = 0.0;
			for (const int j : set) {
				const GroupProxy &gp = level_proxies[j];
				const Vector3 c = gp.node->get_position() + gp.node->get_mesh()->get_aabb().get_center();
				begin = MAX(begin, gp.begin + c.distance_to(origin));
				begin_below += gp.begin / set.size();
			}
			if (proxy_distance > 0.0 && begin >= proxy_distance) {
				continue; // (never drawn: past the distance no proxy is)
			}
			const float scale = CLAMP(float(begin / MAX(begin_below, real_t(1.0))), 1.0f, float(group_cells));
			Node3D *root = memnew(Node3D);
			for (const int j : set) {
				const GroupProxy &gp = level_proxies[j];
				MeshInstance3D *mi = memnew(MeshInstance3D);
				mi->set_mesh(gp.node->get_mesh());
				mi->set_position(gp.node->get_position() - origin);
				if (gp.terrain) {
					mi->set_meta(FellowCellProxies::TERRAIN_META, true);
				}
				root->add_child(mi);
			}
			Dictionary lpi;
			const int objects = triangles - terrain_triangles_below;
			// (Its texture up to twice as wide a level: it stands for many groups' textures, their charts together.)
			const int level_texture_size = MIN(max_texture_size << (level - 1), 4096);
			// (Its objects as far as their count or that error allows, whichever keeps more: a few triangles a building
			// would leave none of its shape. The terrain's error, or 2 pixels with terrain_error 0.)
			const float object_error_per_metre = error_per_metre > 0.0f ? error_per_metre : 4.0f * Math::tan(Math::deg_to_rad(37.5f)) / 1080.0f;
			Ref<ArrayMesh> mesh = FellowCellProxies::bake_proxy(root, ratio, terrain_quads, texel > 0.0f ? texel * scale : 0.0f, level_texture_size, lpi, MAX(1, int(objects / (scale * scale))), float(begin) * error_per_metre, float(begin) * object_error_per_metre);
			memdelete(root);
			if (mesh.is_null()) {
				continue;
			}
			if (lpi.has("texture_width")) {
				level_texture_bytes += int64_t(lpi.get("texture_bytes", 0));
			} else {
				mesh->surface_set_material(0, material);
				texture_skipped = lpi.get("texture_skipped", texture_skipped);
			}
			const String path = p_directory.path_join(vformat("group_l%d_%d_%d_proxy.res", level, keys[si].x, keys[si].y));
			texture_file(mesh, path);
			if (ResourceSaver::save(mesh, path, ResourceSaver::FLAG_COMPRESS) != OK) {
				continue;
			}
			mesh->set_path(path, true);
			MeshInstance3D *gmi = memnew(MeshInstance3D);
			gmi->set_name(vformat("FellowGroupProxy_L%d_%d_%d", level, keys[si].x, keys[si].y));
			gmi->set_mesh(mesh);
			gmi->set_position(origin);
			gmi->set_cast_shadows_setting(proxy_shadows ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			// (Measured to its own centre, which may lie off the boxes' centre: the farthest finer proxy's distance to it.)
			const Vector3 center = origin + mesh->get_aabb().get_center();
			real_t from = 0.0;
			for (const int j : set) {
				const GroupProxy &gp = level_proxies[j];
				const Vector3 c = gp.node->get_position() + gp.node->get_mesh()->get_aabb().get_center();
				from = MAX(from, gp.begin + c.distance_to(center));
			}
			gmi->set_visibility_range_begin(from);
			if (group_fade > 0.0) {
				gmi->set_visibility_range_begin_margin(group_fade);
				gmi->set_visibility_range_fade_mode(GeometryInstance3D::VISIBILITY_RANGE_FADE_DEPENDENCIES);
			}
			if (proxy_distance > 0.0) {
				gmi->set_visibility_range_end(proxy_distance);
			}
			add_child(gmi);
			if (owner) {
				gmi->set_owner(owner);
			}
			Array cell_paths;
			Array children;
			GroupProxy up;
			up.node = gmi;
			up.key = keys[si];
			up.bounds = bounds;
			up.begin = from;
			up.texel = texel > 0.0f ? texel * scale : 0.0f;
			up.triangles = int(lpi.get("proxy_triangles", 0));
			up.terrain = int(lpi.get("proxy_terrain_triangles", 0)) * 2 >= up.triangles;
			for (const int j : set) {
				GroupProxy &gp = level_proxies[j];
				children.push_back(gmi->get_path_to(gp.node));
				gp.node->set_meta(FELLOW_GROUP_PARENT_META, gp.node->get_path_to(gmi));
				gp.node->set_visibility_parent(gp.node->get_path_to(gmi));
				for (FellowStreamingCell3D *cell : gp.cells) {
					cell_paths.push_back(gmi->get_path_to(cell));
					up.cells.push_back(cell);
				}
			}
			gmi->set_meta(FELLOW_GROUP_PROXY_META, cell_paths);
			gmi->set_meta(FELLOW_GROUP_CHILDREN_META, children);
			_update_group(up.cells[0]);
			next.push_back(up);
			baked_level++;
			level_triangles += up.triangles;
		}
		Dictionary li;
		li["level"] = level;
		li["groups"] = baked_level;
		li["triangles"] = level_triangles;
		li["texture_bytes"] = level_texture_bytes;
		if (!texture_skipped.is_empty()) {
			li["texture_skipped"] = texture_skipped;
		}
		level_info.push_back(li);
		level_proxies = next;
	}
	_editor_hide_groups();
	FellowCellProxies::clear_bake_cache();
	info["cells"] = int(targets.size());
	info["proxies"] = proxies;
	info["source_triangles"] = source_triangles;
	info["proxy_triangles"] = proxy_triangles;
	info["textured"] = textured;
	info["texture_bytes"] = texture_bytes;
	info["texture_ms"] = texture_usec / 1000.0;
	info["groups"] = groups_baked;
	info["group_triangles"] = group_triangles;
	info["terrain_triangles"] = terrain_triangles;
	info["group_terrain_triangles"] = group_terrain_triangles;
	info["group_texture_bytes"] = group_texture_bytes;
	info["texture_files"] = texture_files_written; // decision 0263: the textures in files of their own, and their bytes
	info["texture_file_bytes"] = texture_file_bytes;
	info["group_levels"] = level_info; // decision 0262: the coarser levels' groups, triangles, texture bytes
	info["texture_hit_share"] = texture_texels > 0 ? double(texture_hits) / double(texture_texels) : 0.0; // texels coloured by a surface found under them
	info["streamed_meshes"] = streamed;
	info["middle_meshes"] = middle_meshes; // decision 0266: the middle levels' files, their triangles
	info["middle_triangles"] = middle_triangles;
	info["streamed_triangles"] = stream_before;
	info["stand_in_triangles"] = stream_after;
	info["bake_ms"] = (OS::get_singleton()->get_ticks_usec() - t0) / 1000.0;
	return info;
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
	if (!instancing.is_empty() || !cell_fades.is_empty() || !mesh_fades.is_empty()) {
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
	d["update_usec_max"] = update_usec_max;
	d["update_instanced_usec"] = update_instanced_usec;
	d["update_meshes_usec"] = update_meshes_usec;
	d["update_loaded_usec"] = update_loaded_usec;
	d["update_requests_usec"] = update_requests_usec;
	d["frames"] = frames;
	d["settled"] = is_settled();
	int full = 0;
	int middle = 0;
	for (const StreamedMesh &m : streamed_meshes) {
		full += m.level == 2 ? 1 : 0;
		middle += m.level == 1 ? 1 : 0;
	}
	d["streamed_meshes"] = int(streamed_meshes.size());
	d["streamed_meshes_full"] = full;
	d["streamed_meshes_middle"] = middle; // decision 0266
	d["mesh_loads"] = mesh_loads;
	d["mesh_releases"] = mesh_releases;
	d["mesh_swap_usec"] = mesh_swap_usec;
	d["mesh_swap_usec_max"] = mesh_swap_usec_max;
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
		_editor_hide_groups();
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
			} else {
				_editor_hide_groups();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			_update();
			if (!cell_fades.is_empty() || !mesh_fades.is_empty()) {
				_update_fades(get_process_delta_time());
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			in_tree.erase(this);
			_cancel_all();
			_clear_fades();
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
	ClassDB::bind_method(D_METHOD("set_crossfade", "seconds"), &FellowWorldStreamer3D::set_crossfade);
	ClassDB::bind_method(D_METHOD("get_crossfade"), &FellowWorldStreamer3D::get_crossfade);
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
	ClassDB::bind_method(D_METHOD("set_mesh_distance", "distance"), &FellowWorldStreamer3D::set_mesh_distance);
	ClassDB::bind_method(D_METHOD("set_mesh_middle_distance", "distance"), &FellowWorldStreamer3D::set_mesh_middle_distance);
	ClassDB::bind_method(D_METHOD("get_mesh_middle_distance"), &FellowWorldStreamer3D::get_mesh_middle_distance);
	ClassDB::bind_method(D_METHOD("get_mesh_distance"), &FellowWorldStreamer3D::get_mesh_distance);
	ClassDB::bind_method(D_METHOD("set_max_mesh_loads", "count"), &FellowWorldStreamer3D::set_max_mesh_loads);
	ClassDB::bind_method(D_METHOD("get_max_mesh_loads"), &FellowWorldStreamer3D::get_max_mesh_loads);
	ClassDB::bind_method(D_METHOD("bake_proxies", "directory", "options"), &FellowWorldStreamer3D::bake_proxies, DEFVAL(Dictionary()));
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowWorldStreamer3D::get_stats);
	ClassDB::bind_method(D_METHOD("split_into_cells", "root", "cell_size", "directory", "options"), &FellowWorldStreamer3D::split_into_cells, DEFVAL(Dictionary()));

	// Milestone 53: grouped in the inspector.
	ADD_GROUP("Range", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "streaming"), "set_streaming", "is_streaming");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "use_camera"), "set_use_camera", "is_using_camera");
	ADD_PROPERTY(PropertyInfo(Variant::ARRAY, "targets", PROPERTY_HINT_ARRAY_TYPE, "NodePath"), "set_targets", "get_targets");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "load_distance", PROPERTY_HINT_RANGE, "0,10000,0.1,or_greater,suffix:m"), "set_load_distance", "get_load_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "unload_margin", PROPERTY_HINT_RANGE, "0,1000,0.1,or_greater,suffix:m"), "set_unload_margin", "get_unload_margin");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mesh_distance", PROPERTY_HINT_RANGE, "0,10000,0.1,or_greater,suffix:m"), "set_mesh_distance", "get_mesh_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mesh_middle_distance", PROPERTY_HINT_RANGE, "0,10000,0.1,or_greater,suffix:m"), "set_mesh_middle_distance", "get_mesh_middle_distance");
	ADD_GROUP("Budget", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_loading", PROPERTY_HINT_RANGE, "1,64,1"), "set_max_loading", "get_max_loading");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_activations_per_frame", PROPERTY_HINT_RANGE, "1,1024,1"), "set_max_activations_per_frame", "get_max_activations_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_releases_per_frame", PROPERTY_HINT_RANGE, "1,1024,1"), "set_max_releases_per_frame", "get_max_releases_per_frame");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "crossfade", PROPERTY_HINT_RANGE, "0,5,0.01,or_greater,suffix:s"), "set_crossfade", "get_crossfade");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "threaded_instancing"), "set_threaded_instancing", "is_threaded_instancing");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_mesh_loads", PROPERTY_HINT_RANGE, "1,64,1"), "set_max_mesh_loads", "get_max_mesh_loads");
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
	const bool split_terrains = p_options.get("split_terrains", false);
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
		if (FellowTerrain3D *terrain = split_terrains ? Object::cast_to<FellowTerrain3D>(n) : nullptr) {
			// Decision 0173: a terrain into regions of about the cells' size, each a cell of its own; the terrain goes.
			const int quads = MAX(1, int(Math::round(p_cell_size / MAX(terrain->get_vertex_spacing(), 0.001f))));
			const Array regions = terrain->split_into_regions(quads, p_directory, this);
			if (!regions.is_empty()) {
				for (const Variant &r : regions) {
					if (Node *cell = Object::cast_to<Node>(r); cell && cell_owner) {
						cell->set_owner(cell_owner);
					}
					made.push_back(r);
				}
				terrain->get_parent()->remove_child(terrain);
				memdelete(terrain);
			}
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
