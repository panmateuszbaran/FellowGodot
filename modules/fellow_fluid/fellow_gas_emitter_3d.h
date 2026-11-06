/**************************************************************************/
/*  fellow_gas_emitter_3d.h                                               */
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

#include "scene/3d/node_3d.h"

// FellowEngine gas emitter (docs/decisions/0117-gas-simulation.md): a sphere, box or cylinder, a child of a
// FellowGasVolume3D, that adds smoke and fuel to the gas, heats it and pushes it. Fire is fuel and heat: the fuel burns
// where the gas is hotter than the volume's ignition temperature, releasing heat and soot.
class FellowGasEmitter3D : public Node3D {
	GDCLASS(FellowGasEmitter3D, Node3D);

public:
	enum Shape {
		SHAPE_SPHERE,
		SHAPE_BOX,
		SHAPE_CYLINDER,
	};

	enum TemperatureMode {
		TEMPERATURE_SET, // at least this hot inside the emitter
		TEMPERATURE_ADD, // heat added per second
	};

private:
	bool emitting = true;
	Shape shape = SHAPE_SPHERE;
	float radius = 0.25f;
	Vector3 box_size = Vector3(0.5, 0.5, 0.5);
	float height = 0.5f;
	float smoke = 0.5f;
	float temperature = 900.0f;
	TemperatureMode temperature_mode = TEMPERATURE_SET;
	float fuel = 2.0f;
	Vector3 velocity;
	float velocity_strength = 0.0f;
	float noise = 0.5f;
	float noise_scale = 0.3f;

protected:
	static void _bind_methods();
	void _validate_property(PropertyInfo &p_property) const;
	void _notification(int p_what);

public:
	PackedStringArray get_configuration_warnings() const override;

	void set_emitting(bool p_emitting) { emitting = p_emitting; }
	bool is_emitting() const { return emitting; }
	void set_shape(Shape p_shape);
	Shape get_shape() const { return shape; }
	// The setters of what the gizmo draws redraw it (milestone 52).
	void set_radius(float p_radius) {
		radius = MAX(p_radius, 0.0f);
		update_gizmos();
		update_configuration_warnings(); // reaching into the volume's box or not (milestone 54)
	}
	float get_radius() const { return radius; }
	void set_box_size(const Vector3 &p_size) {
		box_size = p_size.max(Vector3());
		update_gizmos();
		update_configuration_warnings();
	}
	Vector3 get_box_size() const { return box_size; }
	void set_height(float p_height) {
		height = MAX(p_height, 0.0f);
		update_gizmos();
		update_configuration_warnings();
	}
	float get_height() const { return height; }
	void set_smoke(float p_smoke) { smoke = MAX(p_smoke, 0.0f); }
	float get_smoke() const { return smoke; }
	void set_temperature(float p_temperature) { temperature = MAX(p_temperature, 0.0f); }
	float get_temperature() const { return temperature; }
	void set_temperature_mode(TemperatureMode p_mode) { temperature_mode = p_mode; }
	TemperatureMode get_temperature_mode() const { return temperature_mode; }
	void set_fuel(float p_fuel) { fuel = MAX(p_fuel, 0.0f); }
	float get_fuel() const { return fuel; }
	void set_velocity(const Vector3 &p_velocity) { velocity = p_velocity; }
	Vector3 get_velocity() const { return velocity; }
	void set_velocity_strength(float p_strength) { velocity_strength = CLAMP(p_strength, 0.0f, 1.0f); }
	float get_velocity_strength() const { return velocity_strength; }
	void set_noise(float p_noise) { noise = CLAMP(p_noise, 0.0f, 1.0f); }
	float get_noise() const { return noise; }
	void set_noise_scale(float p_scale) { noise_scale = MAX(p_scale, 0.001f); }
	float get_noise_scale() const { return noise_scale; }
};

VARIANT_ENUM_CAST(FellowGasEmitter3D::Shape);
VARIANT_ENUM_CAST(FellowGasEmitter3D::TemperatureMode);
