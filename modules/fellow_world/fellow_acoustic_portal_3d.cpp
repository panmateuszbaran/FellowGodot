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

#include "fellow_acoustic_portal_3d.h"

#include "core/object/class_db.h"

LocalVector<FellowAcousticPortal3D *> FellowAcousticPortal3D::portals;

void FellowAcousticPortal3D::set_size(const Vector2 &p_size) {
	size = Vector2(MAX(0.05f, p_size.x), MAX(0.05f, p_size.y));
	update_gizmos();
}

void FellowAcousticPortal3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			portals.push_back(this);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			portals.erase(this);
		} break;
	}
}

void FellowAcousticPortal3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &FellowAcousticPortal3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &FellowAcousticPortal3D::get_size);
	ClassDB::bind_method(D_METHOD("set_transmission", "transmission"), &FellowAcousticPortal3D::set_transmission);
	ClassDB::bind_method(D_METHOD("get_transmission"), &FellowAcousticPortal3D::get_transmission);

	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "transmission", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_transmission", "get_transmission");
}
