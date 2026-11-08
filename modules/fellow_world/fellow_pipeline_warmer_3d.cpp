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

#include "fellow_terrain_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"
#include "core/templates/hashfuncs.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/multimesh_instance_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/multimesh.h"
#include "servers/rendering/rendering_server.h"

// The shelf (decision 0167): pages of SHELF_SIDE x SHELF_SIDE cells, far above the warm-up's place.
static constexpr int SHELF_SIDE = 6;
static constexpr float SHELF_CELL = 2.0f;
static constexpr float SHELF_HEIGHT = 4000.0f;
static constexpr float SHELF_RANGE = 40.0f; // the copies' visibility range: no camera below sees them
static constexpr int SHELF_MAX_ITEMS = 576; // 16 pages

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
	_build_shelf(target);
	_setup_view(0);
	set_process_internal(true);
}

void FellowPipelineWarmer3D::_build_shelf(Node *p_root) {
	shelf_items = 0;
	shelf_pages = 0;
	shelf_list.clear();
	Ref<World3D> world = get_world_3d();
	if (!shelf || p_root == nullptr || world.is_null()) {
		return;
	}
	HashSet<uint64_t> seen;
	auto add = [&](ShelfItem &p_item) {
		Ref<Mesh> mesh;
		Vector<Ref<Material>> surfaces;
		Ref<Material> override_material;
		Ref<Material> overlay;
		if (!_item_state(p_item, mesh, surfaces, override_material, overlay)) {
			return;
		}
		uint64_t h = hash_murmur3_one_64(mesh->get_rid().get_id());
		for (const Ref<Material> &m : surfaces) {
			h = hash_murmur3_one_64(m.is_valid() ? m->get_rid().get_id() : 0, h);
		}
		h = hash_murmur3_one_64(override_material.is_valid() ? override_material->get_rid().get_id() : 0, h);
		h = hash_murmur3_one_64(overlay.is_valid() ? overlay->get_rid().get_id() : 0, h);
		h = hash_murmur3_one_64(uint64_t(p_item.multimesh) | uint64_t(p_item.format) << 1 | uint64_t(p_item.colors) << 3 | uint64_t(p_item.custom) << 4 | uint64_t(p_item.shadows) << 5 | uint64_t(p_item.gi) << 8 | uint64_t(p_item.transparency > 0.0f) << 11 | uint64_t(p_item.layers) << 12, h);
		if (seen.has(h) || shelf_list.size() >= uint32_t(SHELF_MAX_ITEMS)) {
			return;
		}
		seen.insert(h);
		shelf_list.push_back(p_item);
		if (OS::get_singleton()->is_stdout_verbose()) {
			const Object *src = ObjectDB::get_instance(p_item.source);
			const Node *src_node = Object::cast_to<Node>(src);
			print_line(vformat("FellowPipelineWarmer3D shelf %d: %s %s, %d materials%s, from %s", shelf_list.size() - 1, mesh->get_class(), mesh->get_path(), surfaces.size() + int(override_material.is_valid()) + int(overlay.is_valid()), p_item.multimesh ? " (MultiMesh)" : "", src_node ? String(src_node->get_path()) : String()));
		}
	};
	LocalVector<Node *> stack;
	stack.push_back(p_root);
	while (!stack.is_empty()) {
		Node *n = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		if (n == this) {
			continue; // its own viewport and copies
		}
		for (int i = n->get_child_count() - 1; i >= 0; i--) {
			stack.push_back(n->get_child(i));
		}
		// Visible nodes only: a hidden one may not be ready to draw (a burnable's ash heap before its ash: its material's
		// textures are made then).
		GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(n);
		if (gi && gi->is_inside_tree() && gi->is_visible_in_tree() && gi->get_world_3d() == world) {
			// Skinned meshes are drawn from their skeleton's vertices (another format): left out.
			MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(n);
			MultiMeshInstance3D *mmi = Object::cast_to<MultiMeshInstance3D>(n);
			if ((mi && mi->get_skin_reference().is_null()) || mmi) {
				ShelfItem it;
				it.source = n->get_instance_id();
				it.shadows = gi->get_cast_shadows_setting();
				it.gi = gi->get_gi_mode();
				it.layers = gi->get_layer_mask();
				it.transparency = gi->get_transparency();
				if (mmi && mmi->get_multimesh().is_valid()) {
					Ref<MultiMesh> mm = mmi->get_multimesh();
					it.multimesh = true;
					it.format = mm->get_transform_format();
					it.colors = mm->is_using_colors();
					it.custom = mm->is_using_custom_data();
				}
				add(it);
			}
		}
		FellowTerrain3D *t = Object::cast_to<FellowTerrain3D>(n);
		if (t && t->is_visible_in_tree()) {
			// Its foliage: MultiMeshes of 3D transforms (cells scattered around the camera, maybe none in view yet).
			const TypedArray<FellowTerrainFoliage> f = t->get_foliage();
			for (int i = 0; i < f.size(); i++) {
				Ref<FellowTerrainFoliage> kind = f[i];
				if (kind.is_valid()) {
					ShelfItem it;
					it.source = n->get_instance_id();
					it.foliage = i;
					it.multimesh = true;
					it.shadows = kind->is_casting_shadows() ? GeometryInstance3D::SHADOW_CASTING_SETTING_ON : GeometryInstance3D::SHADOW_CASTING_SETTING_OFF;
					it.layers = t->get_render_layer_mask();
					add(it);
				}
			}
		}
	}
	if (shelf_list.is_empty()) {
		return;
	}
	shelf_origin = get_global_position();
	if (from_camera) {
		Camera3D *game_camera = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
		if (game_camera && game_camera != camera) {
			shelf_origin = game_camera->get_global_position();
		}
	}
	shelf_origin.y += SHELF_HEIGHT;
	shelf_root = memnew(Node3D);
	shelf_root->set_name("FellowPipelineShelf");
	add_child(shelf_root, false, INTERNAL_MODE_BACK);
	shelf_root->set_as_top_level(true);
	shelf_root->set_global_transform(Transform3D(Basis(), shelf_origin));
	shelf_items = shelf_list.size();
	shelf_pages = (shelf_items + SHELF_SIDE * SHELF_SIDE - 1) / (SHELF_SIDE * SHELF_SIDE);
}

bool FellowPipelineWarmer3D::_item_state(const ShelfItem &p_item, Ref<Mesh> &r_mesh, Vector<Ref<Material>> &r_surfaces, Ref<Material> &r_override, Ref<Material> &r_overlay) const {
	Object *o = ObjectDB::get_instance(p_item.source);
	if (p_item.foliage >= 0) {
		FellowTerrain3D *t = Object::cast_to<FellowTerrain3D>(o);
		if (t == nullptr) {
			return false;
		}
		const TypedArray<FellowTerrainFoliage> f = t->get_foliage();
		if (p_item.foliage >= f.size()) {
			return false;
		}
		Ref<FellowTerrainFoliage> kind = f[p_item.foliage];
		if (kind.is_null() || kind->get_mesh().is_null()) {
			return false;
		}
		r_mesh = kind->get_mesh();
		return true;
	}
	GeometryInstance3D *gi = Object::cast_to<GeometryInstance3D>(o);
	if (gi == nullptr || !gi->is_inside_tree()) {
		return false;
	}
	r_override = gi->get_material_override();
	r_overlay = gi->get_material_overlay();
	if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(gi)) {
		r_mesh = mi->get_mesh();
		if (r_mesh.is_null()) {
			return false;
		}
		for (int s = 0; s < r_mesh->get_surface_count(); s++) {
			r_surfaces.push_back(mi->get_active_material(s));
		}
		return true;
	}
	if (MultiMeshInstance3D *mmi = Object::cast_to<MultiMeshInstance3D>(gi)) {
		const Ref<MultiMesh> mm = mmi->get_multimesh();
		if (mm.is_null() || mm->get_mesh().is_null()) {
			return false;
		}
		r_mesh = mm->get_mesh();
		return true;
	}
	return false;
}

void FellowPipelineWarmer3D::_show_page(int p_page) {
	for (GeometryInstance3D *copy : page_copies) {
		copy->queue_free();
	}
	page_copies.clear();
	if (p_page < 0 || shelf_root == nullptr) {
		return;
	}
	const int per_page = SHELF_SIDE * SHELF_SIDE;
	for (int i = p_page * per_page; i < MIN(int(shelf_list.size()), (p_page + 1) * per_page); i++) {
		const ShelfItem &it = shelf_list[i];
		GeometryInstance3D *copy = nullptr;
		if (it.multimesh) {
			Ref<MultiMesh> mm;
			mm.instantiate();
			mm->set_transform_format(MultiMesh::TransformFormat(it.format));
			mm->set_use_colors(it.colors);
			mm->set_use_custom_data(it.custom);
			mm->set_instance_count(1);
			if (it.format == MultiMesh::TRANSFORM_3D) {
				mm->set_instance_transform(0, Transform3D());
			} else {
				mm->set_instance_transform_2d(0, Transform2D());
			}
			if (it.colors) {
				mm->set_instance_color(0, Color(1, 1, 1, 1));
			}
			if (it.custom) {
				mm->set_instance_custom_data(0, Color(0, 0, 0, 0));
			}
			MultiMeshInstance3D *mmi = memnew(MultiMeshInstance3D);
			mmi->set_multimesh(mm);
			copy = mmi;
		} else {
			copy = memnew(MeshInstance3D);
		}
		copy->set_cast_shadows_setting(GeometryInstance3D::ShadowCastingSetting(it.shadows));
		copy->set_gi_mode(GeometryInstance3D::GIMode(it.gi));
		copy->set_layer_mask(it.layers);
		copy->set_transparency(it.transparency);
		copy->set_visibility_range_end(SHELF_RANGE);
		copy->set_visible(false); // until _sync_page() gives it its source's mesh and materials
		shelf_root->add_child(copy);
		page_copies.push_back(copy);
	}
	shelf_page_shown = p_page;
	_sync_page();
}

void FellowPipelineWarmer3D::_sync_page() {
	// Every frame a page is shown: each copy draws what its source draws now (a node may change its mesh or materials
	// while the warm-up runs, and free what it no longer uses: a burnable's material when it starts burning).
	const int per_page = SHELF_SIDE * SHELF_SIDE;
	for (uint32_t c = 0; c < page_copies.size(); c++) {
		const int index = shelf_page_shown * per_page + int(c);
		GeometryInstance3D *copy = page_copies[c];
		Ref<Mesh> mesh;
		Vector<Ref<Material>> surfaces;
		Ref<Material> override_material;
		Ref<Material> overlay;
		if (index >= int(shelf_list.size()) || !_item_state(shelf_list[index], mesh, surfaces, override_material, overlay)) {
			copy->set_visible(false);
			continue;
		}
		// Page p's grid in the plane z = 0, centred on x = p * twice its width; the camera looks at it along -z.
		const int cell = int(c);
		const Vector3 centre(shelf_page_shown * SHELF_SIDE * SHELF_CELL * 2.0f + ((cell % SHELF_SIDE) - (SHELF_SIDE - 1) * 0.5f) * SHELF_CELL, ((cell / SHELF_SIDE) - (SHELF_SIDE - 1) * 0.5f) * SHELF_CELL, 0.0f);
		const AABB box = mesh->get_aabb();
		const float scale = SHELF_CELL * 0.8f / MAX(box.get_longest_axis_size(), 0.001f);
		copy->set_transform(Transform3D(Basis().scaled(Vector3(scale, scale, scale)), centre - box.get_center() * scale));
		if (MeshInstance3D *mi = Object::cast_to<MeshInstance3D>(copy)) {
			if (mi->get_mesh() != mesh) {
				mi->set_mesh(mesh);
			}
			for (int s = 0; s < surfaces.size() && s < mi->get_surface_override_material_count(); s++) {
				if (mi->get_surface_override_material(s) != surfaces[s]) {
					mi->set_surface_override_material(s, surfaces[s]);
				}
			}
		} else if (MultiMeshInstance3D *mmi = Object::cast_to<MultiMeshInstance3D>(copy)) {
			if (mmi->get_multimesh()->get_mesh() != mesh) {
				mmi->get_multimesh()->set_mesh(mesh);
			}
		}
		if (copy->get_material_override() != override_material) {
			copy->set_material_override(override_material);
		}
		if (copy->get_material_overlay() != overlay) {
			copy->set_material_overlay(overlay);
		}
		copy->set_visible(true);
	}
}

void FellowPipelineWarmer3D::_setup_view(int p_view) {
	if (p_view >= 6) {
		// A page of the shelf, its whole grid in the view (90 degrees: as far from it as it is wide).
		_show_page(p_view - 6);
		const float width = SHELF_SIDE * SHELF_CELL;
		const Vector3 centre = shelf_origin + Vector3((p_view - 6) * width * 2.0f, 0.0f, 0.0f);
		camera->set_global_transform(Transform3D(Basis(), centre + Vector3(0.0f, 0.0f, width * 0.6f + SHELF_CELL)));
		return;
	}
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
	page_copies.clear(); // freed with their parent
	shelf_list.clear();
	if (shelf_root) {
		shelf_root->queue_free();
		shelf_root = nullptr;
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
			if (view < _view_count()) {
				// Each direction, then each page of the shelf, for a few frames (the first draws ask; TAA's history and
				// the shadows settle).
				if (frame % MAX(1, frames_per_view) == 0) {
					view++;
					if (view < _view_count()) {
						_setup_view(view);
					} else {
						_show_page(-1);
						quiet = 0;
					}
				} else if (view >= 6) {
					_sync_page();
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
				shelf_root = nullptr;
				page_copies.clear();
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
	d["shelf_items"] = shelf_items;
	d["shelf_pages"] = shelf_pages;
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

void FellowPipelineWarmer3D::set_shelf(bool p_shelf) {
	shelf = p_shelf;
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
	ClassDB::bind_method(D_METHOD("set_shelf", "enable"), &FellowPipelineWarmer3D::set_shelf);
	ClassDB::bind_method(D_METHOD("get_shelf"), &FellowPipelineWarmer3D::get_shelf);
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
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shelf"), "set_shelf", "get_shelf");

	ADD_SIGNAL(MethodInfo("finished"));
}

FellowPipelineWarmer3D::FellowPipelineWarmer3D() {
}

FellowPipelineWarmer3D::~FellowPipelineWarmer3D() {
}
