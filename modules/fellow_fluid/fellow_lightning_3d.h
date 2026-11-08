/**************************************************************************/
/*  fellow_lightning_3d.h                                                 */
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

#include "core/math/random_pcg.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"

// FellowEngine lightning (docs/decisions/0129-lightning.md): strikes at random times (`strikes_per_minute`) and
// distances (`distance_range`) around the camera, or when asked (`strike()`). A strike is a few return strokes, each a
// flash decaying in tens of milliseconds: a directional light from the bolt's direction lights the scene (with shadows),
// the bolt — a branching channel, built when it strikes and turned to the camera — glows, and the cloud above it lights
// up. `struck` reports where and how far, for the thunder (it arrives distance / 343 seconds later).
class FellowLightning3D : public Node3D {
	GDCLASS(FellowLightning3D, Node3D);

	float strikes_per_minute = 4.0f;
	Vector2 distance_range = Vector2(400.0f, 3000.0f); // m from the camera
	float cloud_height = 1200.0f; // m above the node
	float energy = 6.0f; // the flash's light energy for a strike 1 km away
	Color color = Color(0.78f, 0.85f, 1.0f);
	float bolt_brightness = 30.0f;
	float bolt_width = 0.006f; // radians: the core's width as seen from the camera (about 6 pixels at 1080p, 55 degrees)
	bool shadows = true;
	float cloud_glow = 0.5f; // decision 0131: how bright the volumetric clouds glow around a flash
	bool cloud_state_pushed = false;
	int64_t seed = 2026;
	double time = 0.0;
	bool time_auto = true;

	// The strike in progress.
	struct Strike {
		bool active = false;
		double start = 0.0;
		int strokes = 0;
		float stroke_time[4] = {};
		float stroke_amplitude[4] = {};
		Vector3 top;
		Vector3 bottom;
		float distance = 0.0f;
	};
	Strike current;
	double last_time = -1.0;
	// What the flash's light and meshes were last given (milestone 52: written again only when it changes).
	bool shown_valid = false;
	bool shown_on = false;
	float shown_energy = 0.0f;
	float shown_env = 0.0f;
	float shown_closeness = 0.0f;
	Color shown_color;
	float shown_brightness = 0.0f;

	DirectionalLight3D *flash = nullptr;
	MeshInstance3D *bolt = nullptr;
	MeshInstance3D *glow = nullptr;
	Ref<ArrayMesh> bolt_mesh;
	Ref<ShaderMaterial> bolt_material;
	Ref<ShaderMaterial> glow_material;
	Ref<Shader> bolt_shader;

	void _build_children();
	void _begin(uint64_t p_seed, float p_distance, float p_azimuth);
	void _build_bolt(RandomPCG &p_rng, const Vector3 &p_camera);
	float _envelope(double p_t) const;
	static uint64_t _hash(uint64_t p_x);
	// Static: it may run on the render thread after the node is gone (its state is in the argument).
	static void _rt_set_cloud_flash(PackedByteArray p_state);
	void _push_cloud_flash(bool p_on, float p_brightness);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static void fellow_bake_shader_codes(Vector<String> &r_codes); // milestone 61: the bolt's and the glow's codes

	// Strikes now (distance < 0: at random within distance_range; azimuth < -360: at random), returns its position.
	Vector3 strike(float p_distance = -1.0f, float p_azimuth_degrees = -1000.0f);
	bool is_striking() const { return current.active; }
	float get_flash() const;

	void set_strikes_per_minute(float p_rate) { strikes_per_minute = MAX(0.0f, p_rate); }
	float get_strikes_per_minute() const { return strikes_per_minute; }
	void set_distance_range(const Vector2 &p_range) {
		distance_range = Vector2(MAX(10.0f, p_range.x), MAX(MAX(10.0f, p_range.x), p_range.y));
		update_gizmos(); // milestone 52
	}
	Vector2 get_distance_range() const { return distance_range; }
	void set_cloud_height(float p_height) {
		cloud_height = MAX(10.0f, p_height);
		update_gizmos();
	}
	float get_cloud_height() const { return cloud_height; }
	void set_energy(float p_energy) { energy = MAX(0.0f, p_energy); }
	float get_energy() const { return energy; }
	void set_color(const Color &p_color) { color = p_color; }
	Color get_color() const { return color; }
	void set_bolt_brightness(float p_brightness) { bolt_brightness = MAX(0.0f, p_brightness); }
	float get_bolt_brightness() const { return bolt_brightness; }
	void set_bolt_width(float p_width) { bolt_width = MAX(0.0001f, p_width); }
	float get_bolt_width() const { return bolt_width; }
	void set_cloud_glow(float p_glow) { cloud_glow = MAX(0.0f, p_glow); }
	float get_cloud_glow() const { return cloud_glow; }
	void set_shadows(bool p_enable);
	bool get_shadows() const { return shadows; }
	void set_seed(int64_t p_seed) { seed = p_seed; }
	int64_t get_seed() const { return seed; }
	void set_time(double p_time) { time = p_time; }
	double get_time() const { return time; }
	void set_time_auto(bool p_auto) { time_auto = p_auto; }
	bool is_time_auto() const { return time_auto; }

	DirectionalLight3D *get_flash_light() const { return flash; }

	FellowLightning3D();
};
