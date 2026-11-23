/**************************************************************************/
/*  fellow_footsteps_3d.h                                                 */
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
#include "scene/3d/skeleton_modifier_3d.h"

// FellowEngine (milestone 181, decision 0323): footsteps synthesised from what the foot does and what it lands on.
// On hard ground a strike is the shoe's sole meeting it — a half-sine of force as long as the sole takes to compress
// (a sneaker's long and soft, a hard heel's short: a click) through the shoe's own resonance and the floor's (a wooden
// floor's boards booming, a steel deck ringing, tiles clicking); on loose ground the foot's weight crushes and shifts
// grains (Cook's PhISEM: the step's energy drives random collisions, each a click through the material's resonances —
// gravel crunching, snow, sand hissing, dry leaves crackling, grass brushing), with the ground's dull compression under
// it; mud squelches and sucks at the foot lifting, water splashes (bubbles ringing, decision 0319's model). Sliding
// feet scuff, a sneaker squeaks on a smooth floor.
class FellowFootstepSynth : public FellowSynthSource {
public:
	enum Ground {
		GROUND_STONE,
		GROUND_WOOD,
		GROUND_METAL,
		GROUND_TILE,
		GROUND_CARPET,
		GROUND_GRAVEL,
		GROUND_SAND,
		GROUND_SNOW,
		GROUND_GRASS,
		GROUND_LEAVES,
		GROUND_DIRT,
		GROUND_MUD,
		GROUND_WATER,
		GROUND_MAX,
	};
	enum Shoe {
		SHOE_SNEAKER,
		SHOE_BOOT,
		SHOE_HARD,
		SHOE_BAREFOOT,
		SHOE_MAX,
	};
	enum Part {
		PART_HEEL, // the first touch
		PART_BALL, // the rest of the sole rolling down
		PART_LIFT, // the toe leaving
	};
	struct Event {
		uint8_t ground[2] = { GROUND_STONE, GROUND_STONE };
		float weight[2] = { 1.0f, 0.0f };
		uint8_t part = PART_HEEL;
		uint8_t shoe = SHOE_SNEAKER;
		float speed = 0.5f; // m/s: the sole coming down (lifting)
		float load = 75.0f; // kg
		int delay = 0; // samples
	};
	struct Scuff {
		uint8_t ground = GROUND_STONE;
		uint8_t shoe = SHOE_SNEAKER;
		float speed = 0.0f; // m/s across the ground
		float load = 75.0f;
	};
	struct Stats {
		uint64_t strikes = 0;
		uint64_t grains = 0;
		uint64_t bubbles = 0;
		float peak = 0.0f;
		bool quiet = true;
	};
	struct GroundInfo {
		const char *name;
		bool loose; // grains
		int resonances;
		float frequency[5];
		float q[5];
		float gain[5];
		float hardness; // the click and the floor's ring (hard surfaces)
		float thump_hz; // the ground's compression under the step
		float thump;
		float grain_rate; // grains a second at a walking step's energy
		float grain_alpha; // Pareto: their amplitudes (lower: spikier)
		float grain_level;
		float press; // s: how long the weight keeps crushing
		float hiss; // brushing (grass, leaves)
		float level; // what the ground radiates (an output gain; milestone 181b: the grounds' balance)
	};
	static const GroundInfo &ground_info(int p_ground);

	void event(const Event &p_event);
	void set_scuff(const Scuff &p_scuff);
	virtual void render(float *r_left, float *r_right, int p_frames) override;
	Stats get_stats();
	void set_seed(uint64_t p_seed) { rng = RandomPCG(p_seed); }
	static FellowSynthSource *make() { return memnew(FellowFootstepSynth); }

private:
	struct Res {
		float a1 = 0.0f, a2 = 0.0f, b = 0.0f, gain = 0.0f;
		float y1 = 0.0f, y2 = 0.0f;
	};
	struct Strike {
		uint8_t ground = 0;
		uint8_t shoe = 0;
		uint8_t part = 0;
		float amp = 0.0f;
		int delay = 0;
		int pos = 0;
		int length = 1; // the force's half-sine (samples)
		int life = 0; // samples left
		float energy = 0.0f; // loose ground: what still crushes grains
		float energy_decay = 0.999f;
		float thump_env = 0.0f;
		float thump_decay = 0.99f;
		float hiss_env = 0.0f;
		float hiss_decay = 0.99f;
		float squelch = 0.0f; // mud: its sweep (0..1)
		float squelch_y = 0.0f;
		Res res[5];
		int nres = 0;
		Res shoe_res;
		FellowOnePole thump_lp1, thump_lp2, hiss_hp, hiss_lp;
		float prev_force = 0.0f;
		float click = 0.0f; // the click's peak
	};
	struct Bubble {
		int delay = 0;
		float phase = 0.0f;
		float f0 = 1000.0f;
		float t = 0.0f;
		float rise = 0.0f;
		float damping = 50.0f;
		float amp = 0.0f;
	};
	LocalVector<Event> posted;
	Scuff posted_scuff;
	Stats stats;
	LocalVector<Strike> strikes;
	LocalVector<Bubble> bubbles;
	Scuff scuff;
	Res scuff_res[3];
	int scuff_ground = -1;
	float squeak_phase = 0.0f;
	float squeak_f = 1800.0f;
	float squeak_env = 0.0f;
	float vibrato = 0.0f;
	float scuff_grains = 0.0f;
	FellowOnePole scuff_hp, scuff_lp;
	RandomPCG rng = RandomPCG(0xf007);
	float _noise() { return rng.randf() * 2.0f - 1.0f; }
	void _tune(Res &r, float p_frequency, float p_q, float p_gain);
	void _start(const Event &p_event, int p_ground, float p_weight);
	void _bubble(int p_delay, float p_radius, float p_amp, float p_rise);
};

// A character's footsteps: a modifier last in its skeleton's stack reads the feet as drawn — each sole's heel and ball
// against the ground under them (a ray: on steps and slopes too) — and hears each touching down (how fast), the sole
// rolling onto the ball, the toe leaving, the foot sliding while down; what it stands on from what the ray hits (its
// FellowContactSound3D's surface or material, a FellowTerrain3D's layers there by their weights, a collider's
// "fellow_surface" metadata, water over the ground). The footstep signal tells a game where and how hard (to let
// guards hear, to print a track in snow, to raise dust).
class FellowFootsteps3D : public SkeletonModifier3D {
	GDCLASS(FellowFootsteps3D, SkeletonModifier3D);

public:
	enum Ground {
		GROUND_STONE = FellowFootstepSynth::GROUND_STONE,
		GROUND_WOOD = FellowFootstepSynth::GROUND_WOOD,
		GROUND_METAL = FellowFootstepSynth::GROUND_METAL,
		GROUND_TILE = FellowFootstepSynth::GROUND_TILE,
		GROUND_CARPET = FellowFootstepSynth::GROUND_CARPET,
		GROUND_GRAVEL = FellowFootstepSynth::GROUND_GRAVEL,
		GROUND_SAND = FellowFootstepSynth::GROUND_SAND,
		GROUND_SNOW = FellowFootstepSynth::GROUND_SNOW,
		GROUND_GRASS = FellowFootstepSynth::GROUND_GRASS,
		GROUND_LEAVES = FellowFootstepSynth::GROUND_LEAVES,
		GROUND_DIRT = FellowFootstepSynth::GROUND_DIRT,
		GROUND_MUD = FellowFootstepSynth::GROUND_MUD,
		GROUND_WATER = FellowFootstepSynth::GROUND_WATER,
		GROUND_MAX = FellowFootstepSynth::GROUND_MAX,
	};
	enum Shoe {
		SHOE_SNEAKER = FellowFootstepSynth::SHOE_SNEAKER,
		SHOE_BOOT = FellowFootstepSynth::SHOE_BOOT,
		SHOE_HARD = FellowFootstepSynth::SHOE_HARD,
		SHOE_BAREFOOT = FellowFootstepSynth::SHOE_BAREFOOT,
	};
	static int ground_from_name(const String &p_name);

private:
	struct Point {
		bool low = false; // within contact_height of the ground
		float height = 1.0f;
		float approach = 0.0f; // m/s: the fastest it came down lately
		Vector3 position;
		float sweep = 0.0f; // m/s across the ground
	};
	struct Foot {
		int foot = -1;
		int toe = -1;
		Vector3 heel_local;
		Vector3 ball_local;
		Point heel;
		Point ball;
		bool down = false; // the foot on the ground
		bool rolled = false; // the second point down since
		double last_event = -10.0;
		double down_since = 0.0;
		bool has_last = false;
		Vector3 last_ankle;
		float since_pose = 0.0f;
		float scuff = 0.0f;
		uint8_t ground[2] = { 0, 0 };
		float weight[2] = { 1.0f, 0.0f };
		float ground_y = 0.0f;
		bool ground_hit = false;
		Vector3 ground_point;
	};
	StringName foot_bones[2] = { StringName("LeftFoot"), StringName("RightFoot") };
	StringName toe_bones[2] = { StringName("LeftToes"), StringName("RightToes") };
	float contact_height = 0.04f;
	float heel_offset = 0.05f;
	uint32_t collision_mask = 1;
	float body_mass = 75.0f;
	Shoe shoe = SHOE_SNEAKER;
	Ground default_ground = GROUND_STONE;
	bool sound_enabled = true;
	float sound_volume_db = 0.0f;
	StringName sound_bus = SNAME("Master");
	float sound_unit_size = 3.0f; // m: the level synthesised heard there (a third-person camera's distance)
	float sound_max_distance = 30.0f;
	Foot feet[2];
	bool bones_valid = false;
	double clock = 0.0;
	FellowAmbientVoice voice;
	double quiet_for = 0.0;
	uint64_t steps = 0;
	uint64_t lifts = 0;
	double scuff_seconds = 0.0;
	int last_ground = -1;
	HashMap<ObjectID, int> ground_cache; // a collider's ground (-1: ask each time: a terrain's layers)

	void _find_bones();
	void _ground_under(Foot &r_foot, const Vector3 &p_at, const Vector3 &p_up);
	void _emit(int p_foot, int p_part, const Vector3 &p_at, float p_speed);

protected:
	static void _bind_methods();
	virtual void _process_modification(double p_delta) override;
	virtual void _skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) override;
	void _notification(int p_what);

public:
	void set_left_foot_bone(const StringName &p_name);
	StringName get_left_foot_bone() const { return foot_bones[0]; }
	void set_right_foot_bone(const StringName &p_name);
	StringName get_right_foot_bone() const { return foot_bones[1]; }
	void set_left_toe_bone(const StringName &p_name);
	StringName get_left_toe_bone() const { return toe_bones[0]; }
	void set_right_toe_bone(const StringName &p_name);
	StringName get_right_toe_bone() const { return toe_bones[1]; }
	void set_contact_height(float p_height) { contact_height = MAX(0.001f, p_height); }
	float get_contact_height() const { return contact_height; }
	void set_heel_offset(float p_offset);
	float get_heel_offset() const { return heel_offset; }
	void set_collision_mask(uint32_t p_mask) { collision_mask = p_mask; }
	uint32_t get_collision_mask() const { return collision_mask; }
	void set_body_mass(float p_mass) { body_mass = CLAMP(p_mass, 1.0f, 2000.0f); }
	float get_body_mass() const { return body_mass; }
	void set_shoe(Shoe p_shoe) { shoe = Shoe(CLAMP(int(p_shoe), 0, int(FellowFootstepSynth::SHOE_MAX) - 1)); }
	Shoe get_shoe() const { return shoe; }
	void set_default_ground(Ground p_ground) { default_ground = Ground(CLAMP(int(p_ground), 0, int(GROUND_MAX) - 1)); }
	Ground get_default_ground() const { return default_ground; }
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

	int get_ground(int p_foot) const;
	Dictionary get_foot_state(int p_foot) const;
	Dictionary get_sound_stats();
	static Dictionary debug_render(const Array &p_events, float p_seconds, float p_rate = 48000.0f, int p_seed = 1);

	virtual PackedStringArray get_configuration_warnings() const override;
};

VARIANT_ENUM_CAST(FellowFootsteps3D::Ground);
VARIANT_ENUM_CAST(FellowFootsteps3D::Shoe);
