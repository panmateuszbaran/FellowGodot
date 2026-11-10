/**************************************************************************/
/*  fellow_terrain_editor.cpp                                             */
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

#include "fellow_terrain_editor.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/editor_undo_redo_manager.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/gui/editor_spin_slider.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/camera_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/button.h"
#include "scene/gui/option_button.h"
#include "scene/gui/separator.h"
#include "scene/resources/image_texture.h"

// The terrain's SculptMode.
static constexpr int SCULPT_RAISE = 0;
static constexpr int SCULPT_LOWER = 1;
static constexpr int SCULPT_SMOOTH = 2;
static constexpr int SCULPT_FLATTEN = 3;

static const char *_tool_names[FellowTerrainEditorPlugin::TOOL_MAX] = { "Raise", "Lower", "Smooth", "Flatten", "Paint", "Holes" };
static const char *_tool_tips[FellowTerrainEditorPlugin::TOOL_MAX] = {
	"Raise the ground (Shift: lower).",
	"Lower the ground (Shift: raise).",
	"Smooth the ground.",
	"Flatten the ground to the height where the stroke starts.",
	"Paint the selected layer.",
	"Cut holes in the ground: not drawn, no collision, no foliage (Shift: fill them).",
};
static const char *_tool_icons[FellowTerrainEditorPlugin::TOOL_MAX] = { "ArrowUp", "ArrowDown", "Blend", "MoveDown", "Paint", "Close" };
static const char *_image_props[4] = { "heightmap", "splat_map", "splat_map_2", "hole_map" };

// The images a tool changes: the heightmap (sculpting), the splat maps (painting) or the hole map.
static bool _tool_uses_image(int p_tool, int p_image) {
	if (p_tool == FellowTerrainEditorPlugin::TOOL_PAINT) {
		return p_image == 1 || p_image == 2;
	}
	if (p_tool == FellowTerrainEditorPlugin::TOOL_HOLE) {
		return p_image == 3;
	}
	return p_image == 0;
}

Node3D *FellowTerrainEditorPlugin::_terrain() const {
	Node3D *t = ObjectDB::get_instance<Node3D>(terrain_id);
	return t && t->is_inside_tree() ? t : nullptr;
}

bool FellowTerrainEditorPlugin::_ray_terrain(Camera3D *p_camera, const Point2 &p_point, Vector3 &r_hit) const {
	Node3D *t = _terrain();
	if (!t || !p_camera) {
		return false;
	}
	const Vector3 from = p_camera->project_ray_origin(p_point);
	const Vector3 dir = p_camera->project_ray_normal(p_point);
	// The ray within the terrain's box, marched in steps of a few texels and refined by bisection.
	const AABB box = t->get_global_transform().xform(AABB(t->call("get_aabb"))).grow(1.0f);
	Vector3 clip_a, clip_b;
	real_t t0 = 0.0f;
	real_t t1 = p_camera->get_far();
	if (!box.has_point(from)) {
		if (!box.intersects_segment(from, from + dir * t1, &clip_a)) {
			return false;
		}
		t0 = (clip_a - from).dot(dir);
	}
	if (box.intersects_segment(from + dir * t1, from + dir * t0, &clip_b)) {
		t1 = (clip_b - from).dot(dir);
	}
	const float spacing = float(t->get("vertex_spacing"));
	const int steps = CLAMP(int((t1 - t0) / (spacing * 2.0f)), 16, 4096);
	const real_t dt = (t1 - t0) / steps;
	auto above = [&](real_t p_t, bool &r_valid) -> bool {
		const Vector3 p = from + dir * p_t;
		const float h = float(t->call("get_height", p));
		r_valid = !Math::is_nan(h);
		return !r_valid || p.y > h;
	};
	bool valid = false;
	bool prev_above = above(t0, valid);
	real_t prev = t0;
	for (int i = 1; i <= steps; i++) {
		const real_t cur = t0 + dt * i;
		const bool a = above(cur, valid);
		if (valid && prev_above && !a) {
			real_t lo = prev;
			real_t hi = cur;
			for (int k = 0; k < 20; k++) {
				const real_t mid = (lo + hi) * 0.5f;
				bool v;
				if (above(mid, v)) {
					lo = mid;
				} else {
					hi = mid;
				}
			}
			r_hit = from + dir * hi;
			return true;
		}
		prev_above = a;
		prev = cur;
	}
	return false;
}

Color FellowTerrainEditorPlugin::_brush_color() const {
	switch (tool) {
		case TOOL_RAISE:
			return Color(0.4, 1.0, 0.4);
		case TOOL_LOWER:
			return Color(1.0, 0.5, 0.3);
		case TOOL_SMOOTH:
			return Color(0.4, 0.8, 1.0);
		case TOOL_FLATTEN:
			return Color(1.0, 0.9, 0.3);
		case TOOL_PAINT:
			return Color(1.0, 0.4, 1.0);
		case TOOL_HOLE:
			return Color(1.0, 0.25, 0.25);
	}
	return Color(1, 1, 1);
}

void FellowTerrainEditorPlugin::_dab(const Vector3 &p_position) {
	Node3D *t = _terrain();
	if (!t) {
		return;
	}
	const float radius = float(size_slider->get_value()) * 0.5f;
	const float strength = float(strength_slider->get_value());
	switch (tool) {
		case TOOL_RAISE:
		case TOOL_LOWER: {
			// Metres a dab at the centre: a twentieth of the brush's size at full strength.
			const bool lower = (tool == TOOL_LOWER) != stroke_inverted;
			t->call("sculpt", p_position, radius, strength * radius * 0.1f, lower ? SCULPT_LOWER : SCULPT_RAISE, 0.0f);
		} break;
		case TOOL_SMOOTH:
			t->call("sculpt", p_position, radius, strength * 0.5f, SCULPT_SMOOTH, 0.0f);
			break;
		case TOOL_FLATTEN:
			t->call("sculpt", p_position, radius, strength * 0.5f, SCULPT_FLATTEN, flatten_height);
			break;
		case TOOL_PAINT:
			t->call("paint", p_position, radius, layer_option->get_selected_id(), strength * 0.5f);
			break;
		case TOOL_HOLE:
			t->call("paint_holes", p_position, radius, !stroke_inverted);
			break;
		default:
			return;
	}
	// The heights' texels the dab may have touched (the splat maps' follow from them at the stroke's end).
	const float spacing = float(t->get("vertex_spacing"));
	const Vector3 l = t->get_global_transform().affine_inverse().xform(p_position);
	const Vector2 c = Vector2(l.x, l.z) / spacing;
	const float rr = MAX(0.5f, radius / spacing) + 2.0f;
	const Rect2i r(Point2i(int(Math::floor(c.x - rr)), int(Math::floor(c.y - rr))), Point2i(int(Math::ceil(2.0f * rr)) + 2, int(Math::ceil(2.0f * rr)) + 2));
	touched = touched.size.x > 0 ? touched.merge(r) : r;
	last_dab = p_position;
}

void FellowTerrainEditorPlugin::_begin_stroke(const Vector3 &p_position, bool p_inverted) {
	Node3D *t = _terrain();
	if (!t) {
		return;
	}
	stroking = true;
	stroke_inverted = p_inverted;
	touched = Rect2i();
	snapshots.clear();
	// The selected terrain and, for a region (decision 0173), its neighbours: the brush reaches them too.
	LocalVector<Node3D *> terrains;
	terrains.push_back(t);
	const Array neighbors = t->call("get_region_neighbors");
	for (const Variant &v : neighbors) {
		if (Node3D *n = Object::cast_to<Node3D>(v)) {
			terrains.push_back(n);
		}
	}
	const float spacing = float(t->get("vertex_spacing"));
	for (Node3D *terrain : terrains) {
		Snapshot s;
		s.terrain = terrain->get_instance_id();
		const Vector3 d = terrain->get_global_transform().affine_inverse().xform(t->get_global_transform().origin);
		s.offset = Vector2i(int(Math::round(d.x / spacing)), int(Math::round(d.z / spacing)));
		for (int i = 0; i < 4; i++) {
			// Only the images this tool changes.
			const bool used = _tool_uses_image(tool, i);
			Ref<Image> img = terrain->get(_image_props[i]);
			s.before[i] = used && img.is_valid() ? Ref<Image>(img->duplicate()) : Ref<Image>();
			// The object itself, to tell an image the terrain replaced (made editable, or made) from one it changed.
			s.before_object[i] = img;
		}
		snapshots.push_back(s);
	}
	const float h = float(t->call("get_height", p_position));
	flatten_height = Math::is_nan(h) ? 0.0f : h;
	_dab(p_position);
}

void FellowTerrainEditorPlugin::_end_stroke() {
	if (!stroking) {
		return;
	}
	stroking = false;
	Node3D *t = _terrain();
	if (!t || touched.size.x <= 0) {
		snapshots.clear();
		return;
	}
	EditorUndoRedoManager *ur = get_undo_redo();
	ur->create_action(tool == TOOL_PAINT ? TTR("Paint Terrain") : (tool == TOOL_HOLE ? TTR("Cut Terrain Holes") : TTR("Sculpt Terrain")), UndoRedo::MERGE_DISABLE, t);
	for (const Snapshot &s : snapshots) {
		Node3D *terrain = ObjectDB::get_instance<Node3D>(s.terrain);
		if (!terrain) {
			continue;
		}
		Ref<Image> hm = terrain->get("heightmap");
		const Vector2i map = hm.is_valid() ? hm->get_size() : Vector2i(1, 1);
		// The stroke's rectangle in this terrain's texels.
		const Rect2i touched_here(touched.position + s.offset, touched.size);
		if (!touched_here.intersects(Rect2i(Point2i(), map))) {
			continue;
		}
		for (int i = 0; i < 4; i++) {
			if (!_tool_uses_image(tool, i)) {
				continue;
			}
			Ref<Image> was = s.before_object[i];
			Ref<Image> now = terrain->get(_image_props[i]);
			if (now.is_null()) {
				continue;
			}
			if (was != now) {
				// The terrain replaced the image (its first edit): the property, both ways (a rebuild).
				ur->add_do_property(terrain, _image_props[i], now);
				ur->add_undo_property(terrain, _image_props[i], was);
				continue;
			}
			// The rectangle in this image's texels (a splat map is stretched over the terrain), both ways.
			Rect2i r = touched_here;
			if (i == 1 || i == 2) {
				const Vector2 sc = Vector2(now->get_size()) / Vector2(MAX(1, map.x), MAX(1, map.y));
				const Point2i a(int(Math::floor(touched_here.position.x * sc.x)) - 2, int(Math::floor(touched_here.position.y * sc.y)) - 2);
				const Point2i b(int(Math::ceil((touched_here.position.x + touched_here.size.x) * sc.x)) + 2, int(Math::ceil((touched_here.position.y + touched_here.size.y) * sc.y)) + 2);
				r = Rect2i(a, b - a);
			}
			r = r.intersection(Rect2i(Point2i(), now->get_size()));
			if (r.size.x <= 0 || r.size.y <= 0 || s.before[i].is_null()) {
				continue;
			}
			Ref<Image> after_region = now->get_region(r);
			Ref<Image> before_region = s.before[i]->get_region(r);
			if (after_region->get_data() == before_region->get_data()) {
				continue; // a neighbour the brush did not reach
			}
			if (i == 0) {
				ur->add_do_method(terrain, "blit_heightmap_region", after_region, r.position);
				ur->add_undo_method(terrain, "blit_heightmap_region", before_region, r.position);
			} else if (i == 3) {
				ur->add_do_method(terrain, "blit_hole_region", after_region, r.position);
				ur->add_undo_method(terrain, "blit_hole_region", before_region, r.position);
			} else {
				ur->add_do_method(terrain, "blit_splat_region", i - 1, after_region, r.position);
				ur->add_undo_method(terrain, "blit_splat_region", i - 1, before_region, r.position);
			}
#ifdef TOOLS_ENABLED
			now->set_edited(true); // an image saved on its own is saved with the scene
#endif
		}
	}
	ur->commit_action(false); // done already
	snapshots.clear();
}

// Decision 0172: the selected terrain's foliage impostors baked into resources next to the scene (impostors/), each
// kind's impostor set in one undoable action once they are all saved.
void FellowTerrainEditorPlugin::_bake_impostors() {
	Node3D *t = _terrain();
	if (!t || bool(t->call("is_baking_impostors"))) {
		return;
	}
	String dir = "res://";
	Node *root = EditorNode::get_singleton()->get_edited_scene();
	if (root && !root->get_scene_file_path().is_empty()) {
		dir = root->get_scene_file_path().get_base_dir();
	}
	dir = dir.path_join("impostors");
	bake_before.clear();
	const Array kinds = t->get("foliage");
	for (int i = 0; i < kinds.size(); i++) {
		Ref<Resource> k = kinds[i];
		bake_before.push_back(k.is_valid() ? Variant(k->get("impostor")) : Variant());
	}
	bake_terrain = t->get_instance_id();
	const Callable done = callable_mp(this, &FellowTerrainEditorPlugin::_impostors_baked);
	if (!t->is_connected("foliage_impostors_baked", done)) {
		t->connect("foliage_impostors_baked", done, CONNECT_ONE_SHOT);
	}
	const int queued = t->call("bake_foliage_impostors", dir);
	if (queued <= 0) {
		t->disconnect("foliage_impostors_baked", done);
		EditorNode::get_singleton()->show_warning(TTR("No foliage kind of this terrain has a mesh and an impostor distance: nothing to bake."));
		return;
	}
	bake_button->set_disabled(true);
	bake_button->set_text(vformat(TTR("Baking %d..."), queued));
}

void FellowTerrainEditorPlugin::_impostors_baked(const PackedStringArray &p_paths) {
	bake_button->set_disabled(false);
	bake_button->set_text(TTR("Bake Impostors"));
	Node3D *t = ObjectDB::get_instance<Node3D>(bake_terrain);
	if (!t) {
		return;
	}
	EditorUndoRedoManager *ur = get_undo_redo();
	ur->create_action(TTR("Bake Foliage Impostors"), UndoRedo::MERGE_DISABLE, t);
	const Array kinds = t->get("foliage");
	for (int i = 0; i < kinds.size() && i < bake_before.size(); i++) {
		Ref<Resource> k = kinds[i];
		if (k.is_null() || k->get("impostor") == bake_before[i]) {
			continue;
		}
		ur->add_do_method(k.ptr(), "set_impostor", k->get("impostor"));
		ur->add_undo_method(k.ptr(), "set_impostor", bake_before[i]);
	}
	ur->commit_action(false); // set already
	bake_before.clear();
	EditorFileSystem::get_singleton()->scan_changes();
	print_line(vformat("FellowTerrain3D: %d impostors baked: %s", p_paths.size(), String(", ").join(p_paths)));
}

void FellowTerrainEditorPlugin::stroke(const PackedVector3Array &p_points, bool p_inverted) {
	if (p_points.is_empty() || tool == TOOL_NONE) {
		return;
	}
	_begin_stroke(p_points[0], p_inverted);
	for (int i = 1; i < p_points.size(); i++) {
		_dab(p_points[i]);
	}
	_end_stroke();
}

EditorPlugin::AfterGUIInput FellowTerrainEditorPlugin::forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) {
	if (tool == TOOL_NONE || !_terrain()) {
		return AFTER_GUI_INPUT_PASS;
	}
	Ref<InputEventMouse> mouse = p_event;
	if (mouse.is_null()) {
		return AFTER_GUI_INPUT_PASS;
	}
	Vector3 hit;
	const bool on_ground = _ray_terrain(p_camera, mouse->get_position(), hit);
	if (on_ground != brush_valid || (on_ground && hit != brush_position)) {
		brush_valid = on_ground;
		brush_position = hit;
		brush_viewport = p_camera->get_viewport() ? p_camera->get_viewport()->get_instance_id() : ObjectID();
		update_overlays();
	}
	Ref<InputEventMouseButton> button = p_event;
	if (button.is_valid() && button->get_button_index() == MouseButton::LEFT) {
		if (button->is_pressed()) {
			if (!on_ground) {
				return AFTER_GUI_INPUT_PASS;
			}
			_begin_stroke(hit, button->is_shift_pressed());
			return AFTER_GUI_INPUT_STOP;
		}
		if (stroking) {
			_end_stroke();
			return AFTER_GUI_INPUT_STOP;
		}
		return AFTER_GUI_INPUT_PASS;
	}
	Ref<InputEventMouseMotion> motion = p_event;
	if (motion.is_valid() && stroking) {
		if (!(motion->get_button_mask().has_flag(MouseButtonMask::LEFT))) {
			_end_stroke(); // released outside the view
			return AFTER_GUI_INPUT_PASS;
		}
		if (on_ground) {
			// Dabs a tenth of the brush's size (its diameter) apart along the way.
			const float spacing = MAX(0.05f, float(size_slider->get_value()) * 0.1f);
			const Vector3 d = hit - last_dab;
			const float len = d.length();
			const int n = MIN(64, int(len / spacing));
			const Vector3 from = last_dab;
			for (int i = 1; i <= n; i++) {
				_dab(from + d * (float(i) / n));
			}
		}
		return AFTER_GUI_INPUT_STOP;
	}
	return AFTER_GUI_INPUT_PASS;
}

void FellowTerrainEditorPlugin::forward_3d_draw_over_viewport(Control *p_overlay) {
	Node3D *t = _terrain();
	if (tool == TOOL_NONE || !t || !brush_valid) {
		return;
	}
	Node3DEditorViewport *vp = Object::cast_to<Node3DEditorViewport>(p_overlay->get_parent());
	Camera3D *cam = vp ? vp->get_camera_3d() : nullptr;
	if (!cam || !cam->get_viewport() || cam->get_viewport()->get_instance_id() != brush_viewport) {
		return;
	}
	// The brush's circle laid on the ground, and a smaller one at half its size (the falloff's middle).
	const float radius = float(size_slider->get_value()) * 0.5f;
	const Color color = _brush_color();
	for (int ring = 0; ring < 2; ring++) {
		const float r = ring == 0 ? radius : radius * 0.5f;
		Vector<Point2> points;
		for (int i = 0; i <= 64; i++) {
			const float a = Math::TAU * i / 64.0f;
			Vector3 p = brush_position + Vector3(Math::cos(a) * r, 0.0f, Math::sin(a) * r);
			const float h = float(t->call("get_height", p));
			if (!Math::is_nan(h)) {
				p.y = h + 0.05f;
			}
			if (cam->is_position_behind(p)) {
				points.clear();
				break;
			}
			points.push_back(cam->unproject_position(p));
		}
		if (points.size() > 1) {
			p_overlay->draw_polyline(points, ring == 0 ? color : color * Color(1, 1, 1, 0.5f), Math::round(2 * EDSCALE), true);
		}
	}
}

bool FellowTerrainEditorPlugin::handles(Object *p_object) const {
	return p_object && p_object->is_class("FellowTerrain3D");
}

void FellowTerrainEditorPlugin::edit(Object *p_object) {
	if (stroking) {
		_end_stroke();
	}
	Node3D *t = Object::cast_to<Node3D>(p_object);
	terrain_id = t && t->is_class("FellowTerrain3D") ? t->get_instance_id() : ObjectID();
	brush_valid = false;
	_update_layers();
}

void FellowTerrainEditorPlugin::make_visible(bool p_visible) {
	if (toolbar) {
		toolbar->set_visible(p_visible);
	}
	if (!p_visible) {
		if (stroking) {
			_end_stroke();
		}
		brush_valid = false;
		update_overlays();
	}
}

void FellowTerrainEditorPlugin::_update_layers() {
	if (!layer_option) {
		return;
	}
	const int selected = MAX(0, layer_option->get_selected_id());
	layer_option->clear();
	Node3D *t = ObjectDB::get_instance<Node3D>(terrain_id);
	Array layers = t ? Array(t->get("layers")) : Array();
	for (int i = 0; i < 9; i++) {
		String name = vformat(TTR("Layer %d"), i);
		if (i < layers.size()) {
			Ref<Resource> l = layers[i];
			if (l.is_valid() && !l->get_name().is_empty()) {
				name = vformat("%d: %s", i, l->get_name());
			}
		} else if (t) {
			name += " " + TTR("(no layer)");
		}
		layer_option->add_item(name, i);
	}
	layer_option->select(MIN(selected, 8));
}

void FellowTerrainEditorPlugin::_tool_toggled(bool p_pressed, int p_tool) {
	if (p_pressed) {
		tool = p_tool;
	} else if (tool == p_tool) {
		tool = TOOL_NONE;
	}
	layer_option->set_disabled(tool != TOOL_PAINT);
	brush_valid = brush_valid && tool != TOOL_NONE;
	update_overlays();
}

void FellowTerrainEditorPlugin::set_tool(int p_tool) {
	ERR_FAIL_COND(p_tool < TOOL_NONE || p_tool >= TOOL_MAX);
	if (p_tool == TOOL_NONE) {
		for (int i = 0; i < TOOL_MAX; i++) {
			tool_buttons[i]->set_pressed(false);
		}
	} else {
		tool_buttons[p_tool]->set_pressed(true);
	}
	tool = p_tool;
	layer_option->set_disabled(tool != TOOL_PAINT);
}

void FellowTerrainEditorPlugin::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			add_control_to_container(CONTAINER_SPATIAL_EDITOR_MENU, toolbar);
			toolbar->hide();
		} break;
		case NOTIFICATION_EXIT_TREE: {
			remove_control_from_container(CONTAINER_SPATIAL_EDITOR_MENU, toolbar);
		} break;
	}
}

void FellowTerrainEditorPlugin::_update_icons() {
	for (int i = 0; i < TOOL_MAX; i++) {
		if (tool_buttons[i]->has_theme_icon(_tool_icons[i], EditorStringName(EditorIcons))) {
			tool_buttons[i]->set_button_icon(tool_buttons[i]->get_theme_icon(_tool_icons[i], EditorStringName(EditorIcons)));
		}
	}
}

void FellowTerrainEditorPlugin::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_tool", "tool"), &FellowTerrainEditorPlugin::set_tool);
	ClassDB::bind_method(D_METHOD("get_tool"), &FellowTerrainEditorPlugin::get_tool);
	ClassDB::bind_method(D_METHOD("stroke", "points", "inverted"), &FellowTerrainEditorPlugin::stroke, DEFVAL(false));
}

FellowTerrainEditorPlugin::FellowTerrainEditorPlugin() {
	toolbar = memnew(HBoxContainer);
	toolbar->set_name("FellowTerrainToolbar");
	toolbar->connect(SceneStringName(theme_changed), callable_mp(this, &FellowTerrainEditorPlugin::_update_icons));
	toolbar->add_child(memnew(VSeparator));
	tool_group.instantiate();
	tool_group->set_allow_unpress(true);
	for (int i = 0; i < TOOL_MAX; i++) {
		Button *b = memnew(Button);
		b->set_name(_tool_names[i]);
		b->set_theme_type_variation(SceneStringName(FlatButton));
		b->set_toggle_mode(true);
		b->set_button_group(tool_group);
		b->set_text(TTRGET(_tool_names[i]));
		b->set_tooltip_text(TTRGET(_tool_tips[i]));
		b->connect(SceneStringName(toggled), callable_mp(this, &FellowTerrainEditorPlugin::_tool_toggled).bind(i));
		toolbar->add_child(b);
		tool_buttons[i] = b;
	}
	layer_option = memnew(OptionButton);
	layer_option->set_name("Layer");
	layer_option->set_tooltip_text(TTR("The layer the Paint tool paints."));
	layer_option->set_disabled(true);
	toolbar->add_child(layer_option);
	size_slider = memnew(EditorSpinSlider);
	size_slider->set_name("Size");
	size_slider->set_label(TTR("Size"));
	size_slider->set_suffix("m");
	size_slider->set_min(0.5);
	size_slider->set_max(256);
	size_slider->set_step(0.5);
	size_slider->set_exp_ratio(true);
	size_slider->set_value(16);
	size_slider->set_custom_minimum_size(Size2(110, 0) * EDSCALE);
	size_slider->set_tooltip_text(TTR("The brush's diameter."));
	toolbar->add_child(size_slider);
	strength_slider = memnew(EditorSpinSlider);
	strength_slider->set_name("Strength");
	strength_slider->set_label(TTR("Strength"));
	strength_slider->set_min(0.01);
	strength_slider->set_max(1);
	strength_slider->set_step(0.01);
	strength_slider->set_value(0.5);
	strength_slider->set_custom_minimum_size(Size2(110, 0) * EDSCALE);
	strength_slider->set_tooltip_text(TTR("How much a dab changes the ground: raising and lowering, a tenth of the brush's radius at its centre at full strength; smoothing, flattening and painting, half of the way at full strength."));
	toolbar->add_child(strength_slider);
	bake_button = memnew(Button);
	bake_button->set_name("BakeImpostors");
	bake_button->set_theme_type_variation(SceneStringName(FlatButton));
	bake_button->set_text(TTR("Bake Impostors"));
	bake_button->set_tooltip_text(TTR("Render the impostors of the foliage kinds with an impostor distance and save them next to the scene (impostors/): from that distance on, their instances are drawn as impostors."));
	bake_button->connect(SceneStringName(pressed), callable_mp(this, &FellowTerrainEditorPlugin::_bake_impostors));
	toolbar->add_child(bake_button);
	_update_layers();
}

FellowTerrainEditorPlugin::~FellowTerrainEditorPlugin() {
	if (toolbar && !toolbar->get_parent()) {
		memdelete(toolbar); // taken out of the 3D editor's menu on exit
	}
}
