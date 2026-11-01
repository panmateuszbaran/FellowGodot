/**************************************************************************/
/*  fellow_wind_3d.h                                                      */
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
#include "scene/resources/image_texture.h"

// FellowEngine wind (docs/decisions/0125-wind.md): the scene's wind, blowing along the node's forward axis (-Z, on the
// ground plane) at `speed`, with gusts — a noise field of cells of `gust_size`, twice as long along the wind,
// travelling with it — that change its speed by up to `gustiness`. Materials with the render mode `fellow_wind` (a
// BaseMaterial3D's Wind group) bend, sway and flutter with it in the vertex shader (Forward+); a FellowRain3D's drops
// follow it. One node at a time is active (the last one to enter the tree).
class FellowWind3D : public Node3D {
	GDCLASS(FellowWind3D, Node3D);

	static FellowWind3D *active;

	float speed = 6.0f; // m/s
	float gustiness = 0.5f; // 0..1
	float gust_size = 25.0f; // m
	float flutter = 1.0f;
	double offset = 0.0; // m: how far the gusts have travelled
	double time = 0.0; // s: the wind's clock (the sway's phase)
	double last_time = -1.0;
	bool time_auto = true;

	Ref<Image> gust_image;
	Ref<ImageTexture> gust_texture;

	void _build_gusts();
	void _push_state(bool p_enabled);
	// Static: it may run on the render thread after the node is gone (its state is in the arguments).
	static void _rt_set(PackedByteArray p_state, RID p_texture);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static FellowWind3D *get_active() { return active; }

	void set_speed(float p_speed) { speed = MAX(0.0f, p_speed); }
	float get_speed() const { return speed; }
	void set_gustiness(float p_gustiness) { gustiness = CLAMP(p_gustiness, 0.0f, 1.0f); }
	float get_gustiness() const { return gustiness; }
	void set_gust_size(float p_size) { gust_size = MAX(0.1f, p_size); }
	float get_gust_size() const { return gust_size; }
	void set_flutter(float p_flutter) { flutter = MAX(0.0f, p_flutter); }
	float get_flutter() const { return flutter; }
	void set_time(double p_time) { time = p_time; }
	double get_time() const { return time; }
	void set_time_auto(bool p_auto) { time_auto = p_auto; }
	bool is_time_auto() const { return time_auto; }

	// The direction the wind blows to (unit, on the ground plane).
	Vector3 get_direction() const;
	// The mean wind's velocity, and the velocity at a point (with the gust there now), in m/s.
	Vector3 get_velocity() const { return get_direction() * speed; }
	Vector3 get_velocity_at(const Vector3 &p_position) const;
	Ref<ImageTexture> get_gust_texture() const { return gust_texture; }
	// How a point maps into the gust texture, as the wind's materials do: (direction x, direction z, the distance the
	// field has travelled (wrapped where it repeats), 1 / (gust size x cells)). uv = ((p.d - offset) / 2, p.side) * w.
	Vector4 get_gust_mapping() const;

	FellowWind3D();
	~FellowWind3D();
};
