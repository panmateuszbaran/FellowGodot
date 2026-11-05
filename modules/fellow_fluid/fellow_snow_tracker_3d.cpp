/**************************************************************************/
/*  fellow_snow_tracker_3d.cpp                                            */
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

#include "fellow_snow_tracker_3d.h"

#include "fellow_node_warnings.h"
#include "fellow_rain_3d.h"

#include "core/object/class_db.h"

PackedStringArray FellowSnowTracker3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	const FellowRain3D *rain = FellowRain3D::get_active();
	if (!rain) {
		warnings.push_back(RTR("Tracks are pressed into a FellowRain3D's snow: the scene has no FellowRain3D."));
	} else if (!rain->get_tracks()) {
		warnings.push_back(RTR("The FellowRain3D's Tracks are off: no tracks are pressed into its snow."));
	} else if (FellowWarnings::mobile() || FellowWarnings::compatibility()) {
		warnings.push_back(RTR("Snow and its tracks are drawn only with the Forward+ renderer."));
	}
	return warnings;
}

LocalVector<FellowSnowTracker3D *> FellowSnowTracker3D::trackers;

bool FellowSnowTracker3D::take_stamp(float r_segment[4], float r_shape[4], float p_snow_depth) {
	if (!enabled || !is_visible_in_tree()) {
		has_last = false;
		return false;
	}
	Transform3D xf = get_global_transform();
	Vector3 p = xf.origin;
	Vector3 from = has_last ? last_position : p;
	if (from.distance_to(p) > 2.0f) {
		from = p; // teleported: no groove across
	}
	last_position = p;
	has_last = true;
	Vector3 f = -xf.basis.get_column(2);
	f.y = 0.0f;
	float fl = f.length();
	Vector3 half = fl > 1e-5f ? f / fl * (length * 0.5f) : Vector3();
	Vector3 a = from - half;
	Vector3 b = p + half;
	r_segment[0] = a.x;
	r_segment[1] = a.z;
	r_segment[2] = b.x;
	r_segment[3] = b.z;
	r_shape[0] = radius;
	r_shape[1] = depth >= 0.0f ? depth : p_snow_depth * 0.8f;
	r_shape[2] = p.y;
	r_shape[3] = contact_height;
	return true;
}

void FellowSnowTracker3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			trackers.push_back(this);
			has_last = false;
		} break;
		case NOTIFICATION_EXIT_TREE: {
			trackers.erase(this);
			has_last = false;
		} break;
	}
}

void FellowSnowTracker3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &FellowSnowTracker3D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &FellowSnowTracker3D::is_enabled);
	ClassDB::bind_method(D_METHOD("set_radius", "metres"), &FellowSnowTracker3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &FellowSnowTracker3D::get_radius);
	ClassDB::bind_method(D_METHOD("set_length", "metres"), &FellowSnowTracker3D::set_length);
	ClassDB::bind_method(D_METHOD("get_length"), &FellowSnowTracker3D::get_length);
	ClassDB::bind_method(D_METHOD("set_depth", "metres"), &FellowSnowTracker3D::set_depth);
	ClassDB::bind_method(D_METHOD("get_depth"), &FellowSnowTracker3D::get_depth);
	ClassDB::bind_method(D_METHOD("set_contact_height", "metres"), &FellowSnowTracker3D::set_contact_height);
	ClassDB::bind_method(D_METHOD("get_contact_height"), &FellowSnowTracker3D::get_contact_height);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0.005,5,0.001,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "length", PROPERTY_HINT_RANGE, "0,10,0.001,suffix:m"), "set_length", "get_length");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "depth", PROPERTY_HINT_RANGE, "-1,2,0.001,suffix:m"), "set_depth", "get_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "contact_height", PROPERTY_HINT_RANGE, "0,5,0.001,suffix:m"), "set_contact_height", "get_contact_height");
}
