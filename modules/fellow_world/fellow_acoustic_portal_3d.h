/**************************************************************************/
/*  register_types.h                                                      */
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

// FellowEngine (milestone 99, decision 0207): an opening sound passes through — a doorway, a window, a hatch. A
// FellowAcoustics3D hears a source blocked from the listener along the path through the nearest open portal between
// them (both legs clear): from the portal's direction, at the path's length, the bend through it costing level and
// highs. The opening is a rectangle of `size` in the node's XY plane, centred on it. Without a FellowAcoustics3D in the
// scene it does nothing.
class FellowAcousticPortal3D : public Node3D {
	GDCLASS(FellowAcousticPortal3D, Node3D);

	static LocalVector<FellowAcousticPortal3D *> portals;

	Vector2 size = Vector2(1.0f, 2.1f);
	float transmission = 1.0f;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static const LocalVector<FellowAcousticPortal3D *> &get_portals() { return portals; }

	void set_size(const Vector2 &p_size);
	Vector2 get_size() const { return size; }
	void set_transmission(float p_transmission) { transmission = CLAMP(p_transmission, 0.0f, 1.0f); }
	float get_transmission() const { return transmission; }
};
