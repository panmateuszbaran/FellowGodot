/**************************************************************************/
/*  fellow_vehicle_effects_3d.h                                           */
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
#include "core/templates/local_vector.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/mesh.h"

class AudioStreamGenerator;
class AudioStreamGeneratorPlayback;
class AudioStreamPlayer3D;
class FellowVehicle3D;
class FellowWheel3D;
class GPUParticles3D;
class MeshInstance3D;
class StandardMaterial3D;

// FellowEngine (milestone 79, decision 0187): what a FellowVehicle3D leaves and sounds like. A child of the vehicle:
// skid marks laid where its tyres slide (a ribbon a wheel, in one mesh in the world, its oldest segments reused),
// smoke from the sliding tyres (a GPU particle system at each), and its engine, tyres and the road synthesised as it
// drives (an AudioStreamPlayer3D on a generator: the firing frequency and its harmonics from the engine's revolutions,
// brighter and louder with the throttle; the tyres' squeal from their sliding; the road's rumble from the speed).
class FellowVehicleEffects3D : public Node3D {
	GDCLASS(FellowVehicleEffects3D, Node3D);

	struct WheelFx {
		ObjectID wheel;
		bool laying = false; // a mark going on
		Vector3 left, right; // its last edge
		float intensity = 0.0f;
		GPUParticles3D *smoke = nullptr;
	};

	struct GlobalStats {
		uint64_t frames = 0;
		uint64_t usec = 0;
		uint64_t segments = 0;
		uint64_t synth_usec = 0;
		uint64_t samples = 0;
	};
	static GlobalStats global_stats;

	bool skid_marks = true;
	int max_mark_segments = 512;
	float mark_width = 0.22f;
	float mark_step = 0.3f;
	float mark_opacity = 0.8f;
	float slip_min = 1.5f; // m/s of sliding: nothing under
	float slip_full = 6.0f; // full marks, smoke and squeal
	float slide_slip = 0.2f; // the combined slip past which a tyre slides (full 0.2 further)
	bool smoke = true;
	int smoke_amount = 40;
	float smoke_lifetime = 2.2f;
	Color smoke_color = Color(0.85f, 0.85f, 0.85f, 0.5f);
	bool sound = true;
	int cylinders = 4;
	float engine_volume_db = 0.0f;
	float tyre_volume_db = -4.0f;
	float road_volume_db = -10.0f;
	Vector3 engine_offset = Vector3(0, 0.6f, -1.3f);
	float sound_unit_size = 8.0f;
	float sound_distance = 80.0f; // m from the listener: farther, silent (nothing synthesised)

	FellowVehicle3D *vehicle = nullptr;
	LocalVector<WheelFx> wheels;

	// The marks: a ring of quads in one mesh, in the world.
	MeshInstance3D *marks = nullptr;
	Ref<ArrayMesh> marks_mesh;
	Ref<StandardMaterial3D> marks_material;
	uint32_t next_segment = 0;
	uint32_t used_segments = 0;
	uint32_t vertex_stride = 0;
	uint32_t attribute_stride = 0;
	uint32_t color_offset = 0;
	AABB marks_aabb;
	bool marks_aabb_valid = false;
	uint64_t segments_laid = 0;
	float mark_low = 0.0f; // the laid segments' lowest and highest points (y)
	float mark_high = 0.0f;

	// The sound.
	AudioStreamPlayer3D *audio = nullptr;
	Ref<AudioStreamGenerator> generator;
	Ref<AudioStreamGeneratorPlayback> playback;
	RandomPCG rng;
	float sample_rate = 44100.0f;
	float phase = 0.0f; // the firing's
	float crank = 0.0f; // the crankshaft's
	float lp_engine = 0.0f;
	float lp_road = 0.0f;
	float bp_x1 = 0.0f, bp_x2 = 0.0f, bp_y1 = 0.0f, bp_y2 = 0.0f;
	float s_rpm = 0.0f, s_load = 0.0f, s_squeal = 0.0f, s_pitch = 0.0f, s_speed = 0.0f;
	float squeal_now = 0.0f;
	float squeal_pitch_now = 0.0f;
	uint64_t last_synth_usec = 0;
	uint64_t last_usec = 0;

	void _setup();
	void _teardown();
	void _add_segment(const Vector3 &p_l0, const Vector3 &p_r0, const Vector3 &p_l1, const Vector3 &p_r1, float p_alpha);
	void _physics_step();
	void _synthesise();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_skid_marks(bool p_enabled) { skid_marks = p_enabled; }
	bool has_skid_marks() const { return skid_marks; }
	void set_max_mark_segments(int p_count);
	int get_max_mark_segments() const { return max_mark_segments; }
	void set_mark_width(float p_width) { mark_width = MAX(0.01f, p_width); }
	float get_mark_width() const { return mark_width; }
	void set_mark_step(float p_step) { mark_step = MAX(0.05f, p_step); }
	float get_mark_step() const { return mark_step; }
	void set_mark_opacity(float p_opacity) { mark_opacity = CLAMP(p_opacity, 0.0f, 1.0f); }
	float get_mark_opacity() const { return mark_opacity; }
	void set_slip_min(float p_speed) { slip_min = MAX(0.0f, p_speed); }
	float get_slip_min() const { return slip_min; }
	void set_slip_full(float p_speed) { slip_full = MAX(0.01f, p_speed); }
	float get_slip_full() const { return slip_full; }
	void set_slide_slip(float p_slip) { slide_slip = MAX(0.0f, p_slip); }
	float get_slide_slip() const { return slide_slip; }
	void set_smoke(bool p_enabled) { smoke = p_enabled; }
	bool has_smoke() const { return smoke; }
	void set_smoke_amount(int p_amount) { smoke_amount = MAX(1, p_amount); }
	int get_smoke_amount() const { return smoke_amount; }
	void set_smoke_lifetime(float p_lifetime) { smoke_lifetime = MAX(0.1f, p_lifetime); }
	float get_smoke_lifetime() const { return smoke_lifetime; }
	void set_smoke_color(const Color &p_color) { smoke_color = p_color; }
	Color get_smoke_color() const { return smoke_color; }
	void set_sound(bool p_enabled);
	bool has_sound() const { return sound; }
	void set_cylinders(int p_count) { cylinders = CLAMP(p_count, 1, 16); }
	int get_cylinders() const { return cylinders; }
	void set_engine_volume_db(float p_db) { engine_volume_db = p_db; }
	float get_engine_volume_db() const { return engine_volume_db; }
	void set_tyre_volume_db(float p_db) { tyre_volume_db = p_db; }
	float get_tyre_volume_db() const { return tyre_volume_db; }
	void set_road_volume_db(float p_db) { road_volume_db = p_db; }
	float get_road_volume_db() const { return road_volume_db; }
	void set_engine_offset(const Vector3 &p_offset) { engine_offset = p_offset; }
	Vector3 get_engine_offset() const { return engine_offset; }
	void set_sound_distance(float p_distance) { sound_distance = MAX(1.0f, p_distance); }
	float get_sound_distance() const { return sound_distance; }
	void set_sound_unit_size(float p_size) { sound_unit_size = MAX(0.1f, p_size); }
	float get_sound_unit_size() const { return sound_unit_size; }

	void clear_marks();
	AudioStreamPlayer3D *get_audio_player() const { return audio; }
	Dictionary get_stats() const;
	static Dictionary get_global_stats();
	static void reset_global_stats();

	PackedStringArray get_configuration_warnings() const override;

	FellowVehicleEffects3D();
	~FellowVehicleEffects3D();
};
