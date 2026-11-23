/**************************************************************************/
/*  fellow_contact_sound_3d.h                                             */
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

#include "fellow_weather_sound.h"

#include "core/templates/hash_map.h"
#include "scene/3d/node_3d.h"

class CollisionObject3D;
class RigidBody3D;

// FellowEngine (milestone 180, decision 0322): a body's sound from its own vibration — modal synthesis (van den Doel,
// Kry and Pai's FoleyAutomatic). The body's modes come from its shape and material (bending of a beam or a plate:
// frequencies from the material's stiffness and density and the body's size, damping from its loss factor); a strike
// drives them with the contact's force (a half-sine as long as the Hertzian contact lasts: hard on hard short and
// bright, soft long and dull), weighted by where it lands (each mode's shape there); sliding drives them with the
// surfaces' roughness sampled at the slip speed, rolling likewise but smoother; the body's own sudden acceleration
// radiates the click of a small hard object.
class FellowModalSynth : public FellowSynthSource {
public:
	static const int MAX_MODES = 32;
	struct Mode {
		float frequency = 1000.0f; // Hz
		float decay = 10.0f; // 1/s (the amplitude's)
		float gain = 1.0f; // full scale for 1 m/s of the mode's velocity
	};
	struct Strike {
		float dv = 0.0f; // m/s into the modes
		float duration = 0.0005f; // s: the contact where it lands (the modes' drive)
		float click = 0.0f; // the body's own pulse, full scale at its peak
		float click_duration = 0.0005f; // s: the body as a whole slowed
		float weights[MAX_MODES] = {};
		int delay = 0; // samples
	};
	struct Contact {
		float slide_speed = 0.0f; // m/s
		float slide_drive = 0.0f; // m/s^2 (the friction's force over the mass)
		float roll_speed = 0.0f;
		float roll_drive = 0.0f;
		float roughness = 1.0f;
		float extra_decay = 0.0f; // 1/s: the body resting on something
		float direct = 0.0f; // the scrape's own noise, full scale at 1 m/s^2 of drive
	};
	struct Stats {
		uint64_t strikes = 0;
		float peak = 0.0f;
		bool quiet = true;
	};

	void set_modes(const LocalVector<Mode> &p_modes, const LocalVector<float> &p_slide_weights);
	void strike(const Strike &p_strike);
	void set_contact(const Contact &p_contact);
	virtual void render(float *r_left, float *r_right, int p_frames) override;
	Stats get_stats();
	void set_seed(uint64_t p_seed) { rng = RandomPCG(p_seed); }
	static FellowSynthSource *make() { return memnew(FellowModalSynth); }

private:
	struct Resonator {
		float a1 = 0.0f, a2 = 0.0f, b = 0.0f, gain = 0.0f;
		float y1 = 0.0f, y2 = 0.0f;
		float frequency = 0.0f, decay = 0.0f, extra = -1.0f;
		float slide_weight = 0.0f;
	};
	struct Pulse {
		Strike strike;
		int pos = 0;
		int length = 1;
		int click_length = 1;
	};
	// Posted (under the lock).
	LocalVector<Mode> posted_modes;
	LocalVector<float> posted_slide_weights;
	bool modes_changed = false;
	LocalVector<Strike> posted_strikes;
	Contact posted_contact;
	Stats stats;
	// The audio thread's.
	LocalVector<Resonator> modes;
	LocalVector<Pulse> pulses;
	Contact contact;
	LocalVector<float> drive; // a block's force on the modes (the strikes')
	LocalVector<float> rough; // a block's surface (the sliding's and rolling's)
	LocalVector<float> clicks;
	RandomPCG rng = RandomPCG(0xc0117ac7);
	FellowPink pink;
	FellowOnePole slide_lp1, slide_lp2, roll_lp1, roll_lp2, direct_hp;
	float energy = 0.0f;
	float _noise() { return rng.randf() * 2.0f - 1.0f; }
	void _tune(Resonator &r, float p_extra);
};

class FellowContactSound3D : public Node3D {
	GDCLASS(FellowContactSound3D, Node3D);

public:
	enum SoundMaterial {
		MATERIAL_WOOD,
		MATERIAL_METAL,
		MATERIAL_STONE,
		MATERIAL_GLASS,
		MATERIAL_PLASTIC,
		MATERIAL_RUBBER,
		MATERIAL_CERAMIC,
		MATERIAL_MAX,
	};
	struct MaterialInfo {
		const char *name;
		float young; // Pa
		float density; // kg/m^3
		float poisson;
		float loss; // the loss factor (a mode at f decays at pi f loss a second)
		float roughness; // how much it scrapes (wood 1)
	};
	static const MaterialInfo &material_info(SoundMaterial p_material);

	// A body's modes from its size (its bounding box: x, y, z in metres), whether it is round across its length (a
	// rod, a pipe), its wall's thickness (0: solid) and its material. Out: modes (frequency, decay; gain 1) sorted, and
	// each mode's shape (the axes and the half-waves along them) for a strike's weights.
	struct ModeShape {
		int8_t axis_u = 0;
		int8_t axis_v = -1;
		uint8_t m = 1;
		uint8_t n = 0;
	};
	static void build_modes(SoundMaterial p_material, const Vector3 &p_size, bool p_round, float p_wall, uint32_t p_seed, int p_max, float p_rate, LocalVector<FellowModalSynth::Mode> &r_modes, LocalVector<ModeShape> &r_shapes);

private:
	SoundMaterial material = MATERIAL_WOOD;
	float wall_thickness = 0.0f; // m, 0: solid
	SoundMaterial ground_material = MATERIAL_STONE;
	int surface = -1; // milestone 181: what a foot stands on (a FellowFootsteps3D::Ground; -1: from the material)
	int mode_count = 24;
	bool sound_enabled = true;
	float sound_volume_db = 0.0f;
	StringName sound_bus = SNAME("Master");
	float sound_unit_size = 1.0f; // m: the synthesis is the pressure at a metre
	float sound_max_distance = 40.0f;

	struct PairState {
		uint64_t last_tick = 0;
		uint64_t last_impact_tick = 0;
		float last_speed = 0.0f;
	};
	HashMap<uint64_t, PairState> pairs;
	HashMap<ObjectID, int> other_material_cache; // -1: none (the ground material)
	LocalVector<FellowModalSynth::Mode> mode_list;
	LocalVector<ModeShape> mode_shapes;
	LocalVector<float> slide_weights;
	AABB box; // the body's shapes, in its space
	bool round = false;
	float volume = 0.0f; // m^3
	float area = 0.0f; // m^2: the largest face
	bool model_dirty = true;
	bool model_posted = false;
	FellowAmbientVoice voice;
	double quiet_for = 0.0;
	uint64_t tick = 0;
	uint64_t impacts = 0;
	uint64_t dropped = 0;
	float last_slide = 0.0f;
	float last_roll = 0.0f;
	double slid_seconds = 0.0;
	double rolled_seconds = 0.0;
	static int active_voices;
	bool counted = false;

	RigidBody3D *_body() const;
	CollisionObject3D *_collision_object() const;
	void _build_model();
	void _physics_tick(double p_dt);
	int _material_of(Object *p_other);
	FellowModalSynth *_voice(bool p_start);
	void _stop_voice();
	void _strike_weights(const Vector3 &p_local, float *r_weights) const;
	float _contact_duration(float p_mass, float p_speed, int p_other) const;
	float _body_duration(float p_mass, float p_contact) const;
	void _make_strike(float p_dv, float p_mass, float p_speed, int p_other, const Vector3 &p_local, FellowModalSynth::Strike &r_strike) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_material(SoundMaterial p_material);
	SoundMaterial get_material() const { return material; }
	void set_wall_thickness(float p_thickness);
	float get_wall_thickness() const { return wall_thickness; }
	void set_surface(int p_surface) { surface = CLAMP(p_surface, -1, 12); }
	int get_surface() const { return surface; }
	int get_ground() const; // milestone 181: the surface, or the material's (stone, wood, metal, tile, carpet)
	void set_ground_material(SoundMaterial p_material) { ground_material = p_material; }
	SoundMaterial get_ground_material() const { return ground_material; }
	void set_mode_count(int p_count);
	int get_mode_count() const { return mode_count; }
	void set_sound_enabled(bool p_enabled);
	bool is_sound_enabled() const { return sound_enabled; }
	void set_sound_volume_db(float p_db);
	float get_sound_volume_db() const { return sound_volume_db; }
	void set_sound_bus(const StringName &p_bus);
	StringName get_sound_bus() const { return sound_bus; }
	void set_sound_unit_size(float p_size);
	float get_sound_unit_size() const { return sound_unit_size; }
	void set_sound_max_distance(float p_distance);
	float get_sound_max_distance() const { return sound_max_distance; }

	Array get_modes();
	Dictionary get_sound_stats();
	Dictionary debug_render(const Array &p_events, float p_seconds, float p_rate = 48000.0f, int p_seed = 1);

	virtual PackedStringArray get_configuration_warnings() const override;

	FellowContactSound3D();
	~FellowContactSound3D();
};

VARIANT_ENUM_CAST(FellowContactSound3D::SoundMaterial);
