/**************************************************************************/
/*  fellow_gas_emitter_3d.cpp                                             */
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

#include "fellow_gas_emitter_3d.h"

#include "fellow_gas_volume_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

void FellowGasEmitter3D::_notification(int p_what) {
	if (p_what == NOTIFICATION_PARENTED || p_what == NOTIFICATION_UNPARENTED || p_what == NOTIFICATION_LOCAL_TRANSFORM_CHANGED) {
		update_configuration_warnings();
	} else if (p_what == NOTIFICATION_ENTER_TREE && Engine::get_singleton()->is_editor_hint()) {
		set_notify_local_transform(true); // moved out of its volume's box: the warning follows (milestone 54)
	}
}

PackedStringArray FellowGasEmitter3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	const FellowGasVolume3D *volume = Object::cast_to<FellowGasVolume3D>(get_parent());
	if (!volume) {
		warnings.push_back(RTR("A FellowGasEmitter3D emits only as a direct child of a FellowGasVolume3D."));
	} else {
		// Milestone 54: the volume's box has its floor on the volume's node; an emitter whose shape (its bounding
		// sphere) does not reach into the box emits nothing.
		const Transform3D t = get_transform();
		const Vector3 s = volume->get_size();
		const Vector3 nearest = t.origin.clamp(Vector3(-s.x * 0.5f, 0.0f, -s.z * 0.5f), Vector3(s.x * 0.5f, s.y, s.z * 0.5f));
		real_t reach = radius;
		if (shape == SHAPE_BOX) {
			reach = (box_size * 0.5f).length();
		} else if (shape == SHAPE_CYLINDER) {
			reach = Math::sqrt(radius * radius + height * height * 0.25f);
		}
		const Vector3 scale = t.basis.get_scale().abs();
		reach *= MAX(scale.x, MAX(scale.y, scale.z));
		if (t.origin.distance_to(nearest) > reach) {
			warnings.push_back(RTR("The emitter is outside its FellowGasVolume3D's box (whose floor is at the volume's origin, its height upwards): it emits nothing."));
		}
	}
	return warnings;
}

void FellowGasEmitter3D::set_shape(Shape p_shape) {
	shape = p_shape;
	notify_property_list_changed();
	update_gizmos();
}

void FellowGasEmitter3D::_validate_property(PropertyInfo &p_property) const {
	if (p_property.name == "radius" && shape == SHAPE_BOX) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	} else if (p_property.name == "box_size" && shape != SHAPE_BOX) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	} else if (p_property.name == "height" && shape != SHAPE_CYLINDER) {
		p_property.usage = PROPERTY_USAGE_NO_EDITOR;
	}
}

void FellowGasEmitter3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_emitting", "emitting"), &FellowGasEmitter3D::set_emitting);
	ClassDB::bind_method(D_METHOD("is_emitting"), &FellowGasEmitter3D::is_emitting);
	ClassDB::bind_method(D_METHOD("set_shape", "shape"), &FellowGasEmitter3D::set_shape);
	ClassDB::bind_method(D_METHOD("get_shape"), &FellowGasEmitter3D::get_shape);
	ClassDB::bind_method(D_METHOD("set_radius", "radius"), &FellowGasEmitter3D::set_radius);
	ClassDB::bind_method(D_METHOD("get_radius"), &FellowGasEmitter3D::get_radius);
	ClassDB::bind_method(D_METHOD("set_box_size", "size"), &FellowGasEmitter3D::set_box_size);
	ClassDB::bind_method(D_METHOD("get_box_size"), &FellowGasEmitter3D::get_box_size);
	ClassDB::bind_method(D_METHOD("set_height", "height"), &FellowGasEmitter3D::set_height);
	ClassDB::bind_method(D_METHOD("get_height"), &FellowGasEmitter3D::get_height);
	ClassDB::bind_method(D_METHOD("set_smoke", "rate"), &FellowGasEmitter3D::set_smoke);
	ClassDB::bind_method(D_METHOD("get_smoke"), &FellowGasEmitter3D::get_smoke);
	ClassDB::bind_method(D_METHOD("set_temperature", "kelvin"), &FellowGasEmitter3D::set_temperature);
	ClassDB::bind_method(D_METHOD("get_temperature"), &FellowGasEmitter3D::get_temperature);
	ClassDB::bind_method(D_METHOD("set_temperature_mode", "mode"), &FellowGasEmitter3D::set_temperature_mode);
	ClassDB::bind_method(D_METHOD("get_temperature_mode"), &FellowGasEmitter3D::get_temperature_mode);
	ClassDB::bind_method(D_METHOD("set_fuel", "rate"), &FellowGasEmitter3D::set_fuel);
	ClassDB::bind_method(D_METHOD("get_fuel"), &FellowGasEmitter3D::get_fuel);
	ClassDB::bind_method(D_METHOD("set_velocity", "velocity"), &FellowGasEmitter3D::set_velocity);
	ClassDB::bind_method(D_METHOD("get_velocity"), &FellowGasEmitter3D::get_velocity);
	ClassDB::bind_method(D_METHOD("set_velocity_strength", "strength"), &FellowGasEmitter3D::set_velocity_strength);
	ClassDB::bind_method(D_METHOD("get_velocity_strength"), &FellowGasEmitter3D::get_velocity_strength);
	ClassDB::bind_method(D_METHOD("set_noise", "amount"), &FellowGasEmitter3D::set_noise);
	ClassDB::bind_method(D_METHOD("get_noise"), &FellowGasEmitter3D::get_noise);
	ClassDB::bind_method(D_METHOD("set_noise_scale", "metres"), &FellowGasEmitter3D::set_noise_scale);
	ClassDB::bind_method(D_METHOD("get_noise_scale"), &FellowGasEmitter3D::get_noise_scale);

	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "emitting"), "set_emitting", "is_emitting");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shape", PROPERTY_HINT_ENUM, "Sphere,Box,Cylinder"), "set_shape", "get_shape");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "radius", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m"), "set_radius", "get_radius");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "box_size", PROPERTY_HINT_NONE, "suffix:m"), "set_box_size", "get_box_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "height", PROPERTY_HINT_RANGE, "0,10,0.01,or_greater,suffix:m"), "set_height", "get_height");
	ADD_GROUP("Emission", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoke", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater,suffix:/s"), "set_smoke", "get_smoke");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fuel", PROPERTY_HINT_RANGE, "0,20,0.01,or_greater,suffix:/s"), "set_fuel", "get_fuel");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "temperature", PROPERTY_HINT_RANGE, "0,3000,1,or_greater,suffix:K"), "set_temperature", "get_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "temperature_mode", PROPERTY_HINT_ENUM, "Set,Add"), "set_temperature_mode", "get_temperature_mode");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_noise", "get_noise");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "noise_scale", PROPERTY_HINT_RANGE, "0.01,5,0.01,or_greater,suffix:m"), "set_noise_scale", "get_noise_scale");
	ADD_GROUP("Velocity", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "velocity", PROPERTY_HINT_NONE, "suffix:m/s"), "set_velocity", "get_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "velocity_strength", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_velocity_strength", "get_velocity_strength");

	BIND_ENUM_CONSTANT(SHAPE_SPHERE);
	BIND_ENUM_CONSTANT(SHAPE_BOX);
	BIND_ENUM_CONSTANT(SHAPE_CYLINDER);
	BIND_ENUM_CONSTANT(TEMPERATURE_SET);
	BIND_ENUM_CONSTANT(TEMPERATURE_ADD);
}
