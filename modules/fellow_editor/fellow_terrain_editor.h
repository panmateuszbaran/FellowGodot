/**************************************************************************/
/*  fellow_terrain_editor.h                                               */
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

#include "editor/plugins/editor_plugin.h"

class Button;
class ButtonGroup;
class EditorSpinSlider;
class HBoxContainer;
class Node3D;
class OptionButton;

// Milestone 58 (decision 0160): sculpting and painting a selected FellowTerrain3D in the 3D view. A toolbar in the 3D
// editor's menu (raise, lower, smooth, flatten, paint, holes (milestone 64); the layer, the brush's size and strength;
// Bake Impostors for the foliage, milestone 64), the brush drawn on
// the ground under the mouse, dabs along a stroke through FellowTerrain3D.sculpt() and paint(), and one undoable action
// a stroke: the touched rectangle of each image before and after it, written back with blit_heightmap_region() and
// blit_splat_region(). Through the terrain's bound methods, so the plugin needs none of fellow_world's headers.
class FellowTerrainEditorPlugin : public EditorPlugin {
	GDCLASS(FellowTerrainEditorPlugin, EditorPlugin);

public:
	enum Tool {
		TOOL_NONE = -1,
		TOOL_RAISE,
		TOOL_LOWER,
		TOOL_SMOOTH,
		TOOL_FLATTEN,
		TOOL_PAINT,
		TOOL_HOLE, // decision 0172
		TOOL_MAX,
	};

private:
	HBoxContainer *toolbar = nullptr;
	Button *tool_buttons[TOOL_MAX] = {};
	Ref<ButtonGroup> tool_group;
	OptionButton *layer_option = nullptr;
	EditorSpinSlider *size_slider = nullptr;
	EditorSpinSlider *strength_slider = nullptr;
	ObjectID terrain_id;
	int tool = TOOL_NONE;

	// The brush under the mouse, in global space.
	bool brush_valid = false;
	Vector3 brush_position;
	ObjectID brush_viewport; // the 3D view it is drawn in

	// A stroke: the images it may change as they were before it, the rectangle of heights' texels it touched.
	bool stroking = false;
	bool stroke_inverted = false;
	Vector3 last_dab;
	float flatten_height = 0.0f;
	Ref<Image> before[4]; // heightmap, splat_map, splat_map_2, hole_map: copies
	Ref<Image> before_object[4]; // the images themselves

	// Baking the foliage's impostors (decision 0172): the kinds' impostors before, for the undo.
	Button *bake_button = nullptr;
	Array bake_before;
	ObjectID bake_terrain;
	void _bake_impostors();
	void _impostors_baked(const PackedStringArray &p_paths);
	Rect2i touched;

	Node3D *_terrain() const;
	bool _ray_terrain(Camera3D *p_camera, const Point2 &p_point, Vector3 &r_hit) const;
	void _dab(const Vector3 &p_position);
	void _begin_stroke(const Vector3 &p_position, bool p_inverted);
	void _end_stroke();
	void _tool_toggled(bool p_pressed, int p_tool);
	void _update_layers();
	Color _brush_color() const;
	void _update_icons();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	String get_plugin_name() const override { return "FellowTerrain3D"; }
	bool has_main_screen() const override { return false; }
	bool handles(Object *p_object) const override;
	void edit(Object *p_object) override;
	void make_visible(bool p_visible) override;
	AfterGUIInput forward_3d_gui_input(Camera3D *p_camera, const Ref<InputEvent> &p_event) override;
	void forward_3d_draw_over_viewport(Control *p_overlay) override;

	// For tests (the editor check): the tool, the brush, a stroke along points of the ground.
	void set_tool(int p_tool);
	int get_tool() const { return tool; }
	void stroke(const PackedVector3Array &p_points, bool p_inverted = false);

	FellowTerrainEditorPlugin();
	~FellowTerrainEditorPlugin();
};
