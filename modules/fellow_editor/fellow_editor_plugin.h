/**************************************************************************/
/*  fellow_editor_plugin.h                                                */
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

#include "core/io/resource_importer.h"
#include "editor/plugins/editor_plugin.h"
#include "editor/scene/3d/node_3d_editor_gizmos.h"

class Button;
class MenuButton;
class ConfirmationDialog;
class Gizmo3DHelper;
class LineEdit;
class SpinBox;

// FellowEngine's 3D nodes in the 3D view (milestone 50): what they cover — a gas volume's box, an emitter's shape, a
// water body's extent, a shallow-water domain, a streaming cell's bounds, the wind's direction and speed, lightning's
// distance band, a snow tracker's radius. Read through their properties, so the plugin needs none of the modules'
// headers. Handles to drag them (milestone 50: the gas volume's; milestone 52: the emitters', the water's, the cells',
// the tracker's and lightning's), undoable.
class FellowGizmoPlugin : public EditorNode3DGizmoPlugin {
	GDCLASS(FellowGizmoPlugin, EditorNode3DGizmoPlugin);

	Ref<Gizmo3DHelper> helper;

	enum HandleKind {
		HANDLES_NONE,
		HANDLES_BOX, // a centred box (Gizmo3DHelper)
		HANDLES_CYLINDER, // a vertical cylinder (Gizmo3DHelper)
		HANDLES_BOUNDS, // an AABB property: a handle on each face, the opposite face kept
		HANDLES_AXES, // AxisHandles
	};

	static void _box_lines(const AABB &p_box, Vector<Vector3> &r_lines);
	static void _circle_lines(const Vector3 &p_centre, real_t p_radius, int p_segments, Vector<Vector3> &r_lines);
	static HandleKind _handle_kind(const Node3D *p_node, const StringName &p_class, StringName &r_size_property);
	static Dictionary _gizmo_properties(const Node3D *p_node, const StringName &p_class);
	Vector<Vector3> _handle_positions(const Node3D *p_node, const StringName &p_class) const;

public:
	// Milestone 52: a handle moving along one of the node's axes, setting one number of a property: the handle sits at
	// base + axis * sign * (value / scale + offset).
	struct AxisHandle {
		StringName property;
		int component = -1; // of a Vector2 property; -1: a float
		int axis = 0;
		real_t sign = 1.0;
		real_t scale = 1.0; // 2: a size centred on the node
		real_t offset = 0.0;
		Vector3 base;
		Vector3 position;
		String name;
	};
	static Vector<AxisHandle> axis_handles_for(const StringName &p_class, const Dictionary &p_properties);

	bool has_gizmo(Node3D *p_node) override;
	String get_gizmo_name() const override;
	int get_priority() const override;
	void redraw(EditorNode3DGizmo *p_gizmo) override;

	String get_handle_name(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const override;
	Variant get_handle_value(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) const override;
	void begin_handle_action(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary) override;
	void set_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, Camera3D *p_camera, const Point2 &p_point) override;
	void commit_handle(const EditorNode3DGizmo *p_gizmo, int p_id, bool p_secondary, const Variant &p_restore, bool p_cancel = false) override;

	// The lines the gizmo draws for a node of p_class with these properties (for tests and the redraw).
	static Vector<Vector3> lines_for(const StringName &p_class, const Dictionary &p_properties);

	FellowGizmoPlugin();
};

// BVH motion capture as an Animation (milestone 50): FellowMotionDatabase.import_bvh() on import, so that a project's
// .bvh files are Animation resources like any other, for FellowMotionDatabase.build() or an AnimationPlayer.
class FellowBVHImporter : public ResourceImporter {
	GDCLASS(FellowBVHImporter, ResourceImporter);

public:
	String get_importer_name() const override;
	String get_visible_name() const override;
	void get_recognized_extensions(List<String> *p_extensions) const override;
	String get_save_extension() const override;
	String get_resource_type() const override;
	int get_preset_count() const override;
	String get_preset_name(int p_idx) const override;
	void get_import_options(const String &p_path, List<ImportOption> *r_options, int p_preset = 0) const override;
	bool get_option_visibility(const String &p_path, const String &p_option, const HashMap<StringName, Variant> &p_options) const override;
	Error import(ResourceUID::ID p_source_id, const String &p_source_file, const String &p_save_path, const HashMap<StringName, Variant> &p_options, List<String> *r_platform_variants, List<String> *r_gen_files = nullptr, Variant *r_metadata = nullptr) override;
	bool can_import_threaded() const override { return false; }
};

// The editor's side of FellowEngine (milestone 50): the gizmos, the BVH importer, and Project > Tools > "Split into
// Streaming Cells..." for the selected FellowWorldStreamer3D (FellowWorldStreamer3D.split_into_cells() on its
// parent's other children). Milestone 52: the 3D editor's toolbar toggle (and editor setting
// editors/3d/fellow/animate_nodes) that lets FellowEngine's nodes animate in the editor or stand still.
class FellowEditorPlugin : public EditorPlugin {
	GDCLASS(FellowEditorPlugin, EditorPlugin);

	Ref<FellowGizmoPlugin> gizmo_plugin;
	Ref<FellowBVHImporter> bvh_importer;
	ConfirmationDialog *split_dialog = nullptr;
	SpinBox *split_cell_size = nullptr;
	LineEdit *split_directory = nullptr;
	ObjectID split_streamer;
	Button *animate_button = nullptr; // milestone 52: the 3D editor's toolbar toggle
	MenuButton *add_menu = nullptr; // milestone 53: ready-made, wired-up setups

public:
	enum Preset {
		PRESET_WEATHER,
		PRESET_FIRE,
		PRESET_SMOKE,
		PRESET_BURNING_BOX,
		PRESET_OCEAN,
		PRESET_LAKE,
		PRESET_POOL,
	};
	// The setup's nodes, not in the tree yet (nullptr for an unknown preset); p_root is the edited scene, where it looks
	// for what the setup can share (the rain, wind and lightning: one of each works at a time).
	static Node3D *make_preset(int p_preset, const Node *p_root);

private:
	void _add_preset(int p_preset);

	void _split_menu();
	void _split_confirmed();
	void _animate_toggled(bool p_animate);
	void _editor_settings_changed();
	void _update_animate_icon();

protected:
	void _notification(int p_what);

public:
	String get_plugin_name() const override { return "FellowEngine"; }

	FellowEditorPlugin();
	~FellowEditorPlugin();
};
