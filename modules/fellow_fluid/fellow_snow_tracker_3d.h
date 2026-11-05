/**************************************************************************/
/*  fellow_snow_tracker_3d.h                                              */
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

// FellowEngine snow tracks (docs/decisions/0130-snow-tracks.md): a presser — a foot, a wheel, a sledge's runner —
// that presses its track into the active FellowRain3D's snow while it touches the scene's top (within contact_height).
// Its track each frame is a capsule of `radius` from where it was to where it is, lengthened by `length` along its
// forward axis (-Z): a foot's print when it stands, a groove when it moves.
class FellowSnowTracker3D : public Node3D {
	GDCLASS(FellowSnowTracker3D, Node3D);

	static LocalVector<FellowSnowTracker3D *> trackers;

	bool enabled = true;
	float radius = 0.1f; // m
	float length = 0.0f; // m along -Z
	float depth = -1.0f; // m (< 0: 0.8 of the snow's depth)
	float contact_height = 0.2f; // m above (or below) the scene's top it still presses
	bool has_last = false;
	Vector3 last_position;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;

	static const LocalVector<FellowSnowTracker3D *> &get_trackers() { return trackers; }
	// Its capsule for this frame (false: none): x0, z0, x1, z1; radius, depth, height, contact height.
	bool take_stamp(float r_segment[4], float r_shape[4], float p_snow_depth);

	void set_enabled(bool p_enabled) { enabled = p_enabled; }
	bool is_enabled() const { return enabled; }
	void set_radius(float p_radius) {
		radius = MAX(0.005f, p_radius);
		update_gizmos(); // milestone 52
	}
	float get_radius() const { return radius; }
	void set_length(float p_length) {
		length = MAX(0.0f, p_length);
		update_gizmos();
	}
	float get_length() const { return length; }
	void set_depth(float p_depth) { depth = p_depth; }
	float get_depth() const { return depth; }
	void set_contact_height(float p_height) { contact_height = MAX(0.0f, p_height); }
	float get_contact_height() const { return contact_height; }
};
