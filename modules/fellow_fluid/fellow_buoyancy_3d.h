/**************************************************************************/
/*  fellow_buoyancy_3d.h                                                  */
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

class FellowWater3D;
class RigidBody3D;

// FellowEngine (milestone 175, decision 0317): a RigidBody3D floating on FellowWater3D's waves. The body's collision
// shapes are filled with points (each a share of their volume); every physics tick each point under the surface (the
// waves as the GPU draws them, evaluated on the CPU) is pushed up by the water it displaces (Archimedes: density x
// gravity x its submerged volume) and dragged towards the water's own motion (the waves' orbital velocity), at its
// place on the body: the body floats at its density's draft, rights itself or capsizes as its shape makes it, and rides
// the waves.
class FellowBuoyancy3D : public Node3D {
	GDCLASS(FellowBuoyancy3D, Node3D);

	struct Point {
		Vector3 local; // in the body's space
		float volume = 0.0f; // m^3
		float half_height = 0.0f; // m: half the point's cell, for its share under the surface
	};

	NodePath water_path;
	float water_density = 1000.0f; // kg/m^3
	float point_spacing = 0.25f; // m
	int max_points = 64;
	float linear_drag = 3.0f; // 1/s, at full submersion
	float angular_drag = 3.0f; // 1/s, at full submersion
	bool follow_waves = true;

	LocalVector<Point> points;
	float total_volume = 0.0f;
	bool points_dirty = true;
	bool approximate_shapes = false;
	float submerged_share = 0.0f;
	uint64_t ticks = 0;
	uint64_t usec = 0;

	RigidBody3D *_body() const;
	FellowWater3D *_water(const Vector3 &p_at) const;
	void _rebuild_points();
	void _physics_tick();

protected:
	static void _bind_methods();
	void _notification(int p_what);

public:
	void set_water_path(const NodePath &p_path) { water_path = p_path; }
	NodePath get_water_path() const { return water_path; }
	void set_water_density(float p_density) { water_density = MAX(1.0f, p_density); }
	float get_water_density() const { return water_density; }
	void set_point_spacing(float p_spacing);
	float get_point_spacing() const { return point_spacing; }
	void set_max_points(int p_points);
	int get_max_points() const { return max_points; }
	void set_linear_drag(float p_drag) { linear_drag = MAX(0.0f, p_drag); }
	float get_linear_drag() const { return linear_drag; }
	void set_angular_drag(float p_drag) { angular_drag = MAX(0.0f, p_drag); }
	float get_angular_drag() const { return angular_drag; }
	void set_follow_waves(bool p_follow) { follow_waves = p_follow; }
	bool is_follow_waves() const { return follow_waves; }

	void rebuild_points() { points_dirty = true; }
	int get_point_count();
	float get_volume();
	float get_submerged_share() const { return submerged_share; }
	Dictionary get_stats() const;

	virtual PackedStringArray get_configuration_warnings() const override;
};
