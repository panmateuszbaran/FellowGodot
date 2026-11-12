/**************************************************************************/
/*  fellow_ladder_3d.cpp                                                  */
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

#include "fellow_ladder_3d.h"

#include "core/object/class_db.h"

LocalVector<FellowLadder3D *> FellowLadder3D::ladders;

float FellowLadder3D::rung_near(float p_height) const {
	const float scale = get_global_transform().basis.get_column(1).length();
	const float spacing = MAX(rung_spacing * scale, 0.05f);
	const float offset = rung_offset * scale;
	const float top = get_global_height();
	const float k = Math::round((p_height - offset) / spacing);
	return CLAMP(offset + k * spacing, MIN(offset, top), top);
}

void FellowLadder3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			ladders.push_back(this);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			ladders.erase(this);
		} break;
	}
}

void FellowLadder3D::set_height(float p_height) {
	height = MAX(0.2f, p_height);
	update_gizmos();
}

void FellowLadder3D::set_width(float p_width) {
	width = MAX(0.1f, p_width);
	update_gizmos();
}

void FellowLadder3D::set_rung_spacing(float p_spacing) {
	rung_spacing = MAX(0.1f, p_spacing);
	update_gizmos();
}

void FellowLadder3D::set_rung_offset(float p_offset) {
	rung_offset = MAX(0.0f, p_offset);
	update_gizmos();
}

void FellowLadder3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_height", "height"), &FellowLadder3D::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &FellowLadder3D::get_height);
	ClassDB::bind_method(D_METHOD("set_width", "width"), &FellowLadder3D::set_width);
	ClassDB::bind_method(D_METHOD("get_width"), &FellowLadder3D::get_width);
	ClassDB::bind_method(D_METHOD("set_rung_spacing", "spacing"), &FellowLadder3D::set_rung_spacing);
	ClassDB::bind_method(D_METHOD("get_rung_spacing"), &FellowLadder3D::get_rung_spacing);
	ClassDB::bind_method(D_METHOD("set_rung_offset", "offset"), &FellowLadder3D::set_rung_offset);
	ClassDB::bind_method(D_METHOD("get_rung_offset"), &FellowLadder3D::get_rung_offset);
	ClassDB::bind_method(D_METHOD("set_exit_at_top", "enabled"), &FellowLadder3D::set_exit_at_top);
	ClassDB::bind_method(D_METHOD("is_exit_at_top"), &FellowLadder3D::is_exit_at_top);
	ClassDB::bind_method(D_METHOD("set_enabled", "enabled"), &FellowLadder3D::set_enabled);
	ClassDB::bind_method(D_METHOD("is_enabled"), &FellowLadder3D::is_enabled);

	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0.2,30,0.01,or_greater,suffix:m"), "set_height", "get_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "width", PROPERTY_HINT_RANGE, "0.1,3,0.01,or_greater,suffix:m"), "set_width", "get_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rung_spacing", PROPERTY_HINT_RANGE, "0.1,1,0.01,suffix:m"), "set_rung_spacing", "get_rung_spacing");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rung_offset", PROPERTY_HINT_RANGE, "0,1,0.01,suffix:m"), "set_rung_offset", "get_rung_offset");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "exit_at_top"), "set_exit_at_top", "is_exit_at_top");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "enabled"), "set_enabled", "is_enabled");
}
