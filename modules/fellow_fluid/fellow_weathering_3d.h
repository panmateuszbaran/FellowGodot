/**************************************************************************/
/*  fellow_weathering_3d.h                                                */
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

class GPUParticlesCollisionHeightField3D;

// FellowEngine weathering (docs/decisions/0332-weathering.md): years of weather on every opaque surface of the
// instances in `cull_mask` (Forward+) — a patina (the albedo's tone varying over metres), dark streaks hanging from the
// ledges above (sills, cornices, balconies, eaves) and thinning down the walls, damp at the foot of the walls up to a
// ragged tide line with salt above it — from the scene's shape seen from above in horizontal layers: a stack of
// heightfields around the camera, `heightfield_size` wide, `heightfield_layers` of `heightfield_layer_height` each up
// from the node's height (one heightfield sees only the highest surface over a point: a cornice would hide the sills
// under it). No textures, no painting: the scene's geometry is enough. One node at a time is active (the last one to
// enter the tree); without it nothing changes.
class FellowWeathering3D : public Node3D {
	GDCLASS(FellowWeathering3D, Node3D);

	static FellowWeathering3D *active;

	float strength = 1.0f;
	float streaks = 1.0f;
	float streak_length = 1.5f; // m
	Color stain_color = Color(0.56f, 0.53f, 0.48f);
	float damp = 1.0f;
	float damp_height = 0.8f; // m
	Color damp_color = Color(0.6f, 0.6f, 0.52f);
	float patina = 0.5f;
	float seed = 0.0f;
	uint32_t cull_mask = 0xFFFFF;

	float heightfield_size = 64.0f; // m
	int heightfield_layers = 16;
	float heightfield_layer_height = 1.75f; // m
	int heightfield_resolution = 1024;
	bool heightfield_always_update = false;

	static constexpr int MAX_LAYERS = 16;
	GPUParticlesCollisionHeightField3D *layers[MAX_LAYERS] = {};
	bool layers_placed = false; // round the camera since the node entered the tree

	float _layer_overlap() const { return MIN(0.1f, 0.25f * heightfield_layer_height); }
	void _build_layers();
	void _update_layers();
	bool _place_layer(int p_layer, const Vector2 &p_xz);
	void _follow_camera(bool p_all);
	void _push_state(bool p_enabled);
	// Static: it may run on the render thread after the node is gone (its state is in the arguments).
	static void _rt_set(PackedByteArray p_state);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;

	static FellowWeathering3D *get_active() { return active; }

	void set_strength(float p_value) { strength = CLAMP(p_value, 0.0f, 1.0f); }
	float get_strength() const { return strength; }
	void set_streaks(float p_value) { streaks = CLAMP(p_value, 0.0f, 1.0f); }
	float get_streaks() const { return streaks; }
	void set_streak_length(float p_value) { streak_length = MAX(0.01f, p_value); }
	float get_streak_length() const { return streak_length; }
	void set_stain_color(const Color &p_color) { stain_color = p_color; }
	Color get_stain_color() const { return stain_color; }
	void set_damp(float p_value) { damp = CLAMP(p_value, 0.0f, 1.0f); }
	float get_damp() const { return damp; }
	void set_damp_height(float p_value) { damp_height = MAX(0.0f, p_value); }
	float get_damp_height() const { return damp_height; }
	void set_damp_color(const Color &p_color) { damp_color = p_color; }
	Color get_damp_color() const { return damp_color; }
	void set_patina(float p_value) { patina = CLAMP(p_value, 0.0f, 1.0f); }
	float get_patina() const { return patina; }
	void set_seed(float p_value) { seed = p_value; }
	float get_seed() const { return seed; }
	void set_cull_mask(uint32_t p_mask);
	uint32_t get_cull_mask() const { return cull_mask; }

	void set_heightfield_size(float p_size);
	float get_heightfield_size() const { return heightfield_size; }
	void set_heightfield_layers(int p_layers);
	int get_heightfield_layers() const { return heightfield_layers; }
	void set_heightfield_layer_height(float p_height);
	float get_heightfield_layer_height() const { return heightfield_layer_height; }
	void set_heightfield_resolution(int p_resolution);
	int get_heightfield_resolution() const { return heightfield_resolution; }
	void set_heightfield_always_update(bool p_always);
	bool is_heightfield_always_update() const { return heightfield_always_update; }

	FellowWeathering3D();
	~FellowWeathering3D();
};
