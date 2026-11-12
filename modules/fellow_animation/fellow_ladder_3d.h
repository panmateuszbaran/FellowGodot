/**************************************************************************/
/*  fellow_ladder_3d.h                                                    */
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

#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"

// FellowEngine (milestone 75, decision 0183): a ladder a FellowTraversal3D climbs — a vertical strip, its foot at the
// node's origin, height up its Y, width along its X, climbed from its +Z side (the climber facing -Z). The rungs every
// rung_spacing from rung_offset up place the hands and feet. Found by the traversals near it (a list of the ladders in
// the tree); it draws nothing (the ladder's mesh is the scene's), the editor draws its outline.
class FellowLadder3D : public Node3D {
	GDCLASS(FellowLadder3D, Node3D);

	static LocalVector<FellowLadder3D *> ladders;

	float height = 3.0f;
	float width = 0.5f;
	float rung_spacing = 0.3f;
	float rung_offset = 0.25f;
	bool exit_at_top = true;
	bool enabled = true;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static const LocalVector<FellowLadder3D *> &get_all() { return ladders; }

	// The ladder's frame (global): its foot, up, the side it is climbed from (towards the climber), across.
	Vector3 get_foot() const { return get_global_transform().origin; }
	Vector3 get_up() const { return get_global_transform().basis.get_column(1).normalized(); }
	Vector3 get_outward() const { return get_global_transform().basis.get_column(2).normalized(); }
	Vector3 get_across() const { return get_global_transform().basis.get_column(0).normalized(); }
	float get_global_height() const { return height * get_global_transform().basis.get_column(1).length(); }
	// The nearest rung's height above the foot to p_height (global metres along the ladder).
	float rung_near(float p_height) const;

	void set_height(float p_height);
	float get_height() const { return height; }
	void set_width(float p_width);
	float get_width() const { return width; }
	void set_rung_spacing(float p_spacing);
	float get_rung_spacing() const { return rung_spacing; }
	void set_rung_offset(float p_offset);
	float get_rung_offset() const { return rung_offset; }
	void set_exit_at_top(bool p_enabled) { exit_at_top = p_enabled; }
	bool is_exit_at_top() const { return exit_at_top; }
	void set_enabled(bool p_enabled) { enabled = p_enabled; }
	bool is_enabled() const { return enabled; }
};
