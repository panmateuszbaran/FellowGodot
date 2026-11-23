/**************************************************************************/
/*  fellow_interior_material_3d.h                                         */
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

#include "scene/resources/material.h"

// FellowEngine (milestone 188, decision 0330): rooms behind windows — interior mapping (van Dongen 2008). The glass
// shows a room the view's ray goes into: its walls, floor and ceiling, what stands against its back wall, furniture in
// it, curtains behind the glass, the day's light falling off into it, a lamp at night; each window's room another by a
// hash of where it is. The rooms are those of each window (its UV2: the metres right of its middle and up from its
// room's floor) or a grid of rooms in the mesh's space. One shader for every instance (made once, as FogMaterial's).
class FellowInteriorMaterial3D : public Material {
	GDCLASS(FellowInteriorMaterial3D, Material);

	bool rooms_from_uv2 = true;
	Vector3 room_size = Vector3(3.6, 3.6, 4.5);
	float floor_height = 0.0f;
	float seed = 0.0f;
	float daylight = 1.0f;
	Color daylight_color = Color(1.0, 0.93, 0.84);
	float lamp_share = 0.0f;
	float lamp_energy = 3.0f;
	Color lamp_color = Color(1.0, 0.7, 0.42);
	float curtain_share = 0.6f;
	float furniture = 1.0f;
	Color glass_tint = Color(0.92, 0.95, 0.93);
	float glass_roughness = 0.08f;
	float grime = 0.25f;
	float reflection = 0.35f;
	Vector2 pane_size;

	static Mutex shader_mutex;
	static RID shader;
	mutable bool shader_set = false;
	static void _update_shader();
	void _param(const char *p_name, const Variant &p_value);

protected:
	static void _bind_methods();

public:
	static const char *get_shader_code();

	void set_rooms_from_uv2(bool p_enable);
	bool is_rooms_from_uv2() const { return rooms_from_uv2; }
	void set_room_size(const Vector3 &p_size);
	Vector3 get_room_size() const { return room_size; }
	void set_floor_height(float p_height);
	float get_floor_height() const { return floor_height; }
	void set_seed(float p_seed);
	float get_seed() const { return seed; }
	void set_daylight(float p_energy);
	float get_daylight() const { return daylight; }
	void set_daylight_color(const Color &p_color);
	Color get_daylight_color() const { return daylight_color; }
	void set_lamp_share(float p_share);
	float get_lamp_share() const { return lamp_share; }
	void set_lamp_energy(float p_energy);
	float get_lamp_energy() const { return lamp_energy; }
	void set_lamp_color(const Color &p_color);
	Color get_lamp_color() const { return lamp_color; }
	void set_curtain_share(float p_share);
	float get_curtain_share() const { return curtain_share; }
	void set_furniture(float p_share);
	float get_furniture() const { return furniture; }
	void set_glass_tint(const Color &p_color);
	Color get_glass_tint() const { return glass_tint; }
	void set_glass_roughness(float p_roughness);
	float get_glass_roughness() const { return glass_roughness; }
	void set_grime(float p_grime);
	float get_grime() const { return grime; }
	void set_reflection(float p_reflection);
	float get_reflection() const { return reflection; }
	void set_pane_size(const Vector2 &p_size);
	Vector2 get_pane_size() const { return pane_size; }

	virtual Shader::Mode get_shader_mode() const override;
	virtual RID get_shader_rid() const override;
	virtual RID get_rid() const override;

	static void cleanup_shader();
	static void fellow_bake_shader_codes(Vector<String> &r_codes);

	FellowInteriorMaterial3D();
	virtual ~FellowInteriorMaterial3D();
};
