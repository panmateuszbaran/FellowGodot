/**************************************************************************/
/*  fellow_editor_plugin.cpp                                              */
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

#include "fellow_editor_plugin.h"

#include "core/io/resource_importer.h"
#include "core/io/resource_saver.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "editor/editor_data.h"
#include "editor/editor_interface.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_file_system.h"
#include "editor/scene/3d/gizmos/gizmo_3d_helper.h"
#include "editor/scene/3d/node_3d_editor_plugin.h"
#include "editor/themes/editor_scale.h"
#include "scene/3d/node_3d.h"
#include "scene/gui/box_container.h"
#include "scene/gui/dialogs.h"
#include "scene/gui/label.h"
#include "scene/gui/line_edit.h"
#include "scene/gui/spin_box.h"
#include "scene/resources/animation.h"
#include "scene/scene_string_names.h"

// ------------------------------------------------------------------ gizmos

static const char *FELLOW_GIZMO_CLASSES[] = {
	"FellowGasVolume3D",
	"FellowGasEmitter3D",
	"FellowWater3D",
	"FellowShallowWater3D",
	"FellowStreamingCell3D",
	"FellowWind3D",
	"FellowLightning3D",
	"FellowSnowTracker3D",
	"FellowRain3D",
	"FellowWorldStreamer3D",
	"FellowHLOD3D",
	"FellowBurnable3D",
	"FellowMotionCharacter3D",
};

// The properties each class's lines are drawn from.
static Vector<StringName> _fellow_gizmo_properties(const StringName &p_class) {
	if (p_class == SNAME("FellowGasVolume3D")) {
		return { SNAME("size") };
	} else if (p_class == SNAME("FellowGasEmitter3D")) {
		return { SNAME("shape"), SNAME("radius"), SNAME("box_size"), SNAME("height") };
	} else if (p_class == SNAME("FellowWater3D")) {
		return { SNAME("extent") };
	} else if (p_class == SNAME("FellowShallowWater3D")) {
		return { SNAME("domain_size") };
	} else if (p_class == SNAME("FellowStreamingCell3D")) {
		return { SNAME("bounds"), SNAME("load_distance") };
	} else if (p_class == SNAME("FellowWind3D")) {
		return { SNAME("speed") };
	} else if (p_class == SNAME("FellowLightning3D")) {
		return { SNAME("distance_range"), SNAME("cloud_height") };
	} else if (p_class == SNAME("FellowSnowTracker3D")) {
		return { SNAME("radius"), SNAME("length") };
	}
	return {};
}

static StringName _fellow_gizmo_class(const Node3D *p_node) {
	for (const char *c : FELLOW_GIZMO_CLASSES) {
		if (p_node->is_class(c)) {
			return StringName(c);
		}
	}
	return StringName();
}

void FellowGizmoPlugin::_box_lines(const AABB &p_box, Vector<Vector3> &r_lines) {
	for (int i = 0; i < 12; i++) {
		Vector3 a, b;
		p_box.get_edge(i, a, b);
		r_lines.push_back(a);
		r_lines.push_back(b);
	}
}

void FellowGizmoPlugin::_circle_lines(const Vector3 &p_centre, real_t p_radius, int p_segments, Vector<Vector3> &r_lines) {
	for (int i = 0; i < p_segments; i++) {
		const real_t a0 = Math::TAU * i / p_segments;
		const real_t a1 = Math::TAU * (i + 1) / p_segments;
		r_lines.push_back(p_centre + Vector3(Math::cos(a0), 0, Math::sin(a0)) * p_radius);
		r_lines.push_back(p_centre + Vector3(Math::cos(a1), 0, Math::sin(a1)) * p_radius);
	}
}

Vector<Vector3> FellowGizmoPlugin::lines_for(const StringName &p_class, const Dictionary &p_properties) {
	Vector<Vector3> lines;
	if (p_class == SNAME("FellowGasVolume3D")) {
		const Vector3 size = p_properties.get("size", Vector3(1, 1, 1));
		_box_lines(AABB(-size * 0.5, size), lines);
	} else if (p_class == SNAME("FellowGasEmitter3D")) {
		const int shape = p_properties.get("shape", 0);
		const real_t radius = p_properties.get("radius", 0.5);
		if (shape == 1) { // box
			const Vector3 size = p_properties.get("box_size", Vector3(1, 1, 1));
			_box_lines(AABB(-size * 0.5, size), lines);
		} else if (shape == 2) { // cylinder (vertical)
			const real_t h = p_properties.get("height", 1.0);
			_circle_lines(Vector3(0, -h * 0.5, 0), radius, 24, lines);
			_circle_lines(Vector3(0, h * 0.5, 0), radius, 24, lines);
			for (int i = 0; i < 4; i++) {
				const real_t a = Math::TAU * i / 4;
				const Vector3 d(Math::cos(a) * radius, 0, Math::sin(a) * radius);
				lines.push_back(d + Vector3(0, -h * 0.5, 0));
				lines.push_back(d + Vector3(0, h * 0.5, 0));
			}
		} else { // sphere: three great circles
			_circle_lines(Vector3(), radius, 32, lines);
			for (int i = 0; i < 32; i++) {
				const real_t a0 = Math::TAU * i / 32;
				const real_t a1 = Math::TAU * (i + 1) / 32;
				lines.push_back(Vector3(Math::cos(a0), Math::sin(a0), 0) * radius);
				lines.push_back(Vector3(Math::cos(a1), Math::sin(a1), 0) * radius);
				lines.push_back(Vector3(0, Math::sin(a0), Math::cos(a0)) * radius);
				lines.push_back(Vector3(0, Math::sin(a1), Math::cos(a1)) * radius);
			}
		}
	} else if (p_class == SNAME("FellowWater3D") || p_class == SNAME("FellowShallowWater3D")) {
		const Vector2 e = p_properties.get(p_class == SNAME("FellowWater3D") ? "extent" : "domain_size", Vector2());
		if (e.x > 0 && e.y > 0) {
			const Vector3 h(e.x * 0.5, 0, e.y * 0.5);
			const Vector3 c[4] = { Vector3(-h.x, 0, -h.z), Vector3(h.x, 0, -h.z), Vector3(h.x, 0, h.z), Vector3(-h.x, 0, h.z) };
			for (int i = 0; i < 4; i++) {
				lines.push_back(c[i]);
				lines.push_back(c[(i + 1) % 4]);
			}
		}
	} else if (p_class == SNAME("FellowStreamingCell3D")) {
		// The cell's bounds, and with its own load distance the box the camera loads it from.
		const AABB bounds = p_properties.get("bounds", AABB());
		_box_lines(bounds, lines);
		const real_t d = p_properties.get("load_distance", 0.0);
		if (d > 0.0) {
			_box_lines(bounds.grow(d), lines);
		}
	} else if (p_class == SNAME("FellowWind3D")) {
		// An arrow along -Z (the wind blows the way the node faces), as long as the wind's speed (1-20 m).
		const real_t len = CLAMP(real_t(p_properties.get("speed", 5.0)), 1.0, 20.0);
		const Vector3 tip(0, 0, -len);
		lines.push_back(Vector3());
		lines.push_back(tip);
		const real_t head = MIN(len * 0.25, 2.0);
		lines.push_back(tip);
		lines.push_back(tip + Vector3(head * 0.5, 0, head));
		lines.push_back(tip);
		lines.push_back(tip + Vector3(-head * 0.5, 0, head));
	} else if (p_class == SNAME("FellowLightning3D")) {
		// Strikes fall between the two rings around the camera at play time (drawn around the node), from the clouds.
		const Vector2 range = p_properties.get("distance_range", Vector2(400, 3000));
		const real_t h = p_properties.get("cloud_height", 1200.0);
		_circle_lines(Vector3(), range.x, 64, lines);
		_circle_lines(Vector3(), range.y, 96, lines);
		lines.push_back(Vector3(range.x, 0, 0));
		lines.push_back(Vector3(range.x, h, 0));
	} else if (p_class == SNAME("FellowSnowTracker3D")) {
		// A capsule along the node's -Z, centred: the track it presses into the snow.
		const real_t r = p_properties.get("radius", 0.1);
		const real_t h = real_t(p_properties.get("length", 0.0)) * 0.5;
		_circle_lines(Vector3(0, 0, -h), r, 24, lines);
		if (h > 0.0) {
			_circle_lines(Vector3(0, 0, h), r, 24, lines);
			lines.push_back(Vector3(r, 0, -h));
			lines.push_back(Vector3(r, 0, h));
			lines.push_back(Vector3(-r, 0, -h));
			lines.push_back(Vector3(-r, 0, h));
		}
	}
	return lines;
}

FellowGizmoPlugin::FellowGizmoPlugin() {
	helper.instantiate();
	create_material("fellow_lines", Color(0.35, 0.85, 1.0));
	create_material("fellow_cell", Color(1.0, 0.7, 0.25));
	create_handle_material("handles");
	for (const char *c : FELLOW_GIZMO_CLASSES) {
		const Ref<Theme> theme = EditorNode::get_singleton()->get_editor_theme();
		if (theme.is_valid() && theme->has_icon(c, EditorStringName(EditorIcons))) {
			create_icon_material(String("icon_") + c, theme->get_icon(c, EditorStringName(EditorIcons)));
		}
	}
}

bool FellowGizmoPlugin::has_gizmo(Node3D *p_node) {
	return _fellow_gizmo_class(p_node) != StringName();
}

String FellowGizmoPlugin::get_gizmo_name() const {
	return "FellowEngine";
}

int FellowGizmoPlugin::get_priority() const {
	return -1;
}

void FellowGizmoPlugin::redraw(EditorNode3DGizmo *p_gizmo) {
	p_gizmo->clear();
	Node3D *node = p_gizmo->get_node_3d();
	const StringName cls = _fellow_gizmo_class(node);
	Dictionary props;
	for (const StringName &p : _fellow_gizmo_properties(cls)) {
		props[p] = node->get(p);
	}
	const Vector<Vector3> lines = lines_for(cls, props);
	if (!lines.is_empty()) {
		p_gizmo->add_lines(lines, get_material(cls == SNAME("FellowStreamingCell3D") ? "fellow_cell" : "fellow_lines", p_gizmo));
		p_gizmo->add_collision_segments(lines);
	}
	if (cls == SNAME("FellowGasVolume3D")) {
		p_gizmo->add_handles(helper->box_get_handles(node->get("size")), get_material("handles"));
	}
	const String icon = String("icon_") + String(cls);
	if (materials.has(icon)) {
		p_gizmo->add_unscaled_billboard(get_material(icon, p_gizmo), 0.05);
	}
}

String FellowGizmoPlugin::get_handle_name(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const {
	return helper->box_get_handle_name(p_id);
}

Variant FellowGizmoPlugin::get_handle_value(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const {
	return p_gizmo->get_node_3d()->get("size");
}

void FellowGizmoPlugin::begin_handle_action(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) {
	helper->initialize_handle_action(get_handle_value(p_gizmo, p_id, p_secondary), p_gizmo->get_node_3d()->get_global_transform());
}

void FellowGizmoPlugin::set_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, Camera3D *p_camera, const Point2 &p_point) {
	Node3D *node = p_gizmo->get_node_3d();
	Vector3 size = node->get("size");
	Vector3 sg[2];
	helper->get_segment(p_camera, p_point, sg);
	Vector3 position;
	helper->box_set_handle(sg, p_id, size, position);
	node->set("size", size);
	node->set_global_position(position);
}

void FellowGizmoPlugin::commit_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, const Variant &p_restore, bool p_cancel) {
	helper->box_commit_handle(TTR("Change Gas Volume Size"), p_cancel, p_gizmo->get_node_3d());
}

// ------------------------------------------------------------------ BVH importer

String FellowBVHImporter::get_importer_name() const {
	return "fellow.bvh";
}

String FellowBVHImporter::get_visible_name() const {
	return "BVH Motion Capture (FellowEngine)";
}

void FellowBVHImporter::get_recognized_extensions(List<String> *p_extensions) const {
	p_extensions->push_back("bvh");
}

String FellowBVHImporter::get_save_extension() const {
	return "res";
}

String FellowBVHImporter::get_resource_type() const {
	return "Animation";
}

int FellowBVHImporter::get_preset_count() const {
	return 2;
}

String FellowBVHImporter::get_preset_name(int p_idx) const {
	return p_idx == 1 ? "CMU (Hahne's conversion)" : "Centimetres";
}

void FellowBVHImporter::get_import_options(const String &p_path, List<ImportOption> *r_options, int p_preset) const {
	// The BVH's units in metres: centimetres by default; the CMU database's conversion is in 1/0.45 inch.
	r_options->push_back(ImportOption(PropertyInfo(Variant::FLOAT, "scale", PROPERTY_HINT_RANGE, "0.0001,10,0.000001,or_greater"), p_preset == 1 ? 0.056444 : 0.01));
	// The first frame of many captures is a T-pose: left out of the animation.
	r_options->push_back(ImportOption(PropertyInfo(Variant::BOOL, "skip_first_frame"), p_preset == 1));
}

bool FellowBVHImporter::get_option_visibility(const String &p_path, const String &p_option, const HashMap<StringName, Variant> &p_options) const {
	return true;
}

Error FellowBVHImporter::import(ResourceUID::ID p_source_id, const String &p_source_file, const String &p_save_path, const HashMap<StringName, Variant> &p_options, List<String> *r_platform_variants, List<String> *r_gen_files, Variant *r_metadata) {
	MethodBind *import_bvh = ClassDB::get_method("FellowMotionDatabase", "import_bvh");
	ERR_FAIL_NULL_V_MSG(import_bvh, ERR_UNAVAILABLE, "BVH import needs FellowEngine's fellow_animation module.");
	const Variant path = p_source_file;
	const Variant scale = p_options.has("scale") ? p_options["scale"] : Variant(0.01);
	const Variant skip = p_options.has("skip_first_frame") ? p_options["skip_first_frame"] : Variant(false);
	const Variant *args[3] = { &path, &scale, &skip };
	Callable::CallError ce;
	const Dictionary result = import_bvh->call(nullptr, args, 3, ce);
	Ref<Animation> animation = result.get("animation", Variant());
	ERR_FAIL_COND_V_MSG(animation.is_null(), ERR_PARSE_ERROR, "Cannot read the BVH file " + p_source_file + ".");
	animation->set_name(p_source_file.get_file().get_basename());
	return ResourceSaver::save(animation, p_save_path + ".res");
}

// ------------------------------------------------------------------ the plugin

void FellowEditorPlugin::_split_menu() {
	Node *streamer = nullptr;
	for (Node *n : EditorInterface::get_singleton()->get_selection()->get_top_selected_node_list()) {
		if (n->is_class("FellowWorldStreamer3D")) {
			streamer = n;
			break;
		}
	}
	if (!streamer || !Object::cast_to<Node3D>(streamer->get_parent())) {
		EditorNode::get_singleton()->show_warning(TTR("Select a FellowWorldStreamer3D whose parent is the level's root: its parent's other 3D children are split into cells."));
		return;
	}
	split_streamer = streamer->get_instance_id();
	Node *scene = EditorInterface::get_singleton()->get_edited_scene_root();
	const String base = scene && !scene->get_scene_file_path().is_empty() ? scene->get_scene_file_path().get_basename() : String("res://level");
	split_directory->set_text(base + "_cells");
	split_dialog->popup_centered(Size2(460, 0) * EDSCALE);
}

void FellowEditorPlugin::_split_confirmed() {
	Node *streamer = ObjectDB::get_instance<Node>(split_streamer);
	ERR_FAIL_NULL(streamer);
	Node3D *root = Object::cast_to<Node3D>(streamer->get_parent());
	ERR_FAIL_NULL(root);
	Dictionary options;
	options["owner"] = EditorInterface::get_singleton()->get_edited_scene_root();
	const Array cells = streamer->call("split_into_cells", root, split_cell_size->get_value(), split_directory->get_text(), options);
	EditorFileSystem::get_singleton()->scan_changes();
	EditorInterface::get_singleton()->mark_scene_as_unsaved();
	EditorNode::get_singleton()->show_accept(vformat(TTR("%d streaming cells made; their scenes are in %s."), cells.size(), split_directory->get_text()), TTR("OK"));
}

void FellowEditorPlugin::_notification(int p_what) {
	if (p_what == NOTIFICATION_ENTER_TREE) {
		add_tool_menu_item(TTR("Split into Streaming Cells (FellowEngine)..."), callable_mp(this, &FellowEditorPlugin::_split_menu));
	} else if (p_what == NOTIFICATION_EXIT_TREE) {
		remove_tool_menu_item(TTR("Split into Streaming Cells (FellowEngine)..."));
	}
}

FellowEditorPlugin::FellowEditorPlugin() {
	// The gizmos and the importer from the start, as the editor's own plugins do (the importer before the first scan).
	gizmo_plugin.instantiate();
	Node3DEditor::get_singleton()->add_gizmo_plugin(gizmo_plugin);
	bvh_importer.instantiate();
	ResourceFormatImporter::get_singleton()->add_importer(bvh_importer);

	split_dialog = memnew(ConfirmationDialog);
	split_dialog->set_title(TTR("Split into Streaming Cells"));
	VBoxContainer *vb = memnew(VBoxContainer);
	split_dialog->add_child(vb);
	Label *info = memnew(Label);
	info->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	info->set_custom_minimum_size(Size2(420, 0) * EDSCALE);
	info->set_text(TTR("The streamer's parent's 3D children are grouped into cells on a grid, each cell saved as a scene and replaced by a FellowStreamingCell3D under the streamer. Cameras, directional lights, environments and nodes wider than two cells stay. This cannot be undone: save the scene first."));
	vb->add_child(info);
	split_cell_size = memnew(SpinBox);
	split_cell_size->set_min(1);
	split_cell_size->set_max(100000);
	split_cell_size->set_step(1);
	split_cell_size->set_value(64);
	split_cell_size->set_suffix("m");
	vb->add_margin_child(TTR("Cell size:"), split_cell_size);
	split_directory = memnew(LineEdit);
	vb->add_margin_child(TTR("Directory for the cells' scenes:"), split_directory);
	split_dialog->connect(SceneStringName(confirmed), callable_mp(this, &FellowEditorPlugin::_split_confirmed));
	EditorNode::get_singleton()->get_gui_base()->add_child(split_dialog);
}

FellowEditorPlugin::~FellowEditorPlugin() {
	if (ResourceFormatImporter::get_singleton()) {
		ResourceFormatImporter::get_singleton()->remove_importer(bvh_importer);
	}
}
