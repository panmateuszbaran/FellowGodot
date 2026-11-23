/**************************************************************************/
/*  fellow_footsteps_3d.cpp                                               */
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

#include "fellow_footsteps_3d.h"

#include "fellow_contact_sound_3d.h"
#include "fellow_water_3d.h"
#include "fellow_water_sound.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "core/templates/hash_set.h"
#include "scene/3d/physics/collision_object_3d.h"
#include "scene/3d/skeleton_3d.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/audio/audio_server.h"
#include "servers/physics_3d/physics_server_3d.h"

static const int FOOT_BLOCK = 32;
static const float FOOT_LEVEL = 0.1f; // a walking step's strength (full scale, at sound_unit_size)

static _FORCE_INLINE_ float foot_soft_limit(float p_x) {
	const float ax = Math::abs(p_x);
	return ax > 0.6f ? SIGN(p_x) * (0.6f + 0.4f * Math::tanh((ax - 0.6f) / 0.4f)) : p_x;
}

// The grounds: resonances (Hz, Q, gain), how hard (the click, the ring), the compression's thump, the grains (a
// second at a walking step's energy, their Pareto exponent, level), how long the weight crushes, brushing.
const FellowFootstepSynth::GroundInfo &FellowFootstepSynth::ground_info(int p_ground) {
	static const GroundInfo infos[GROUND_MAX] = {
		// name, loose, resonances (Hz, Q, gain), hardness, thump (Hz, level), grains (a second, Pareto, level), press, hiss, level
		{ "stone", false, 1, { 900.0f }, { 2.0f }, { 0.3f }, 1.0f, 120.0f, 0.2f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f },
		{ "wood", false, 5, { 95.0f, 165.0f, 290.0f, 470.0f, 820.0f }, { 5.0f, 7.0f, 9.0f, 8.0f, 6.0f }, { 0.5f, 0.4f, 0.3f, 0.22f, 0.15f }, 0.8f, 110.0f, 0.3f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f },
		{ "metal", false, 5, { 230.0f, 560.0f, 930.0f, 1560.0f, 2380.0f }, { 60.0f, 90.0f, 80.0f, 100.0f, 90.0f }, { 1.0f, 0.8f, 0.7f, 0.5f, 0.4f }, 1.0f, 120.0f, 0.3f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f },
		{ "tile", false, 2, { 1800.0f, 3100.0f }, { 6.0f, 8.0f }, { 0.6f, 0.5f }, 1.2f, 130.0f, 0.15f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f },
		{ "carpet", false, 1, { 110.0f }, { 0.8f }, { 0.5f }, 0.12f, 90.0f, 0.12f, 0.0f, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f },
		{ "gravel", true, 3, { 2100.0f, 3400.0f, 5200.0f }, { 4.0f, 5.0f, 4.0f }, { 1.0f, 0.8f, 0.6f }, 0.0f, 140.0f, 0.3f, 1200.0f, 1.6f, 0.5f, 0.16f, 0.0f, 1.0f },
		{ "sand", true, 2, { 2800.0f, 5600.0f }, { 1.2f, 1.4f }, { 1.0f, 0.7f }, 0.0f, 110.0f, 0.2f, 4000.0f, 3.5f, 1.0f, 0.2f, 0.0f, 1.0f },
		{ "snow", true, 3, { 900.0f, 2300.0f, 4100.0f }, { 1.8f, 2.5f, 3.0f }, { 1.0f, 0.8f, 0.5f }, 0.0f, 90.0f, 0.5f, 1800.0f, 2.2f, 0.7f, 0.24f, 0.0f, 1.0f },
		{ "grass", true, 2, { 2500.0f, 5000.0f }, { 1.0f, 1.2f }, { 1.0f, 0.7f }, 0.0f, 120.0f, 0.25f, 2500.0f, 3.0f, 0.5f, 0.14f, 0.2f, 1.0f },
		{ "leaves", true, 3, { 2600.0f, 4400.0f, 6000.0f }, { 3.0f, 3.5f, 3.0f }, { 1.0f, 0.8f, 0.5f }, 0.0f, 120.0f, 0.25f, 500.0f, 1.4f, 0.9f, 0.22f, 0.12f, 1.0f },
		{ "dirt", true, 1, { 1300.0f }, { 1.5f }, { 1.0f }, 0.0f, 110.0f, 1.0f, 200.0f, 2.0f, 0.4f, 0.1f, 0.0f, 1.0f },
		{ "mud", true, 1, { 600.0f }, { 1.2f }, { 1.0f }, 0.0f, 80.0f, 0.9f, 80.0f, 2.0f, 0.2f, 0.2f, 0.0f, 1.0f },
		{ "water", true, 0, {}, {}, {}, 0.0f, 90.0f, 0.3f, 0.0f, 2.0f, 0.0f, 0.1f, 0.0f, 1.0f },
	};
	return infos[CLAMP(p_ground, 0, int(GROUND_MAX) - 1)];
}

// The shoes: how long the sole takes to compress at 0.5 m/s, its own resonance (Hz, Q, gain), its click.
struct FellowShoeInfo {
	float tau;
	float frequency;
	float q;
	float gain;
	float click;
};
static const FellowShoeInfo &shoe_info(int p_shoe) {
	static const FellowShoeInfo infos[FellowFootstepSynth::SHOE_MAX] = {
		{ 0.002f, 700.0f, 1.2f, 0.6f, 0.3f }, // a sneaker: soft rubber
		{ 0.001f, 1100.0f, 2.0f, 0.8f, 0.6f }, // a boot: a harder sole, a heel
		{ 0.00035f, 2600.0f, 4.0f, 1.0f, 1.0f }, // a hard heel (leather, wood): a click
		{ 0.003f, 300.0f, 0.8f, 0.5f, 0.5f }, // bare feet: the flesh's slap
	};
	return infos[CLAMP(p_shoe, 0, int(FellowFootstepSynth::SHOE_MAX) - 1)];
}

void FellowFootstepSynth::event(const Event &p_event) {
	MutexLock lock(mutex);
	if (posted.size() < 32) {
		posted.push_back(p_event);
	}
	stats.quiet = false;
}

void FellowFootstepSynth::set_scuff(const Scuff &p_scuff) {
	MutexLock lock(mutex);
	posted_scuff = p_scuff;
	if (p_scuff.speed > 0.0f) {
		stats.quiet = false;
	}
}

FellowFootstepSynth::Stats FellowFootstepSynth::get_stats() {
	MutexLock lock(mutex);
	return stats;
}

void FellowFootstepSynth::_tune(Res &r, float p_frequency, float p_q, float p_gain) {
	const float f = MIN(p_frequency, rate * 0.45f);
	const float d = float(Math::PI) * f / MAX(p_q, 0.3f);
	const float rr = Math::exp(-d / rate);
	const float theta = float(Math::TAU) * f / rate;
	r.a1 = 2.0f * rr * Math::cos(theta);
	r.a2 = rr * rr;
	r.b = Math::sin(theta);
	r.gain = p_gain;
	r.y1 = 0.0f;
	r.y2 = 0.0f;
}

void FellowFootstepSynth::_bubble(int p_delay, float p_radius, float p_amp, float p_rise) {
	if (bubbles.size() >= 48) {
		return;
	}
	Bubble b;
	b.delay = MAX(0, p_delay);
	b.f0 = MIN(FellowWaterSynth::minnaert_frequency(p_radius), rate * 0.4f);
	b.damping = FellowWaterSynth::bubble_damping(p_radius);
	b.amp = p_amp;
	b.rise = p_rise;
	bubbles.push_back(b);
}

// A strike's strength: the sole's speed (the pressure goes with it), the weight's root, the part of the step.
void FellowFootstepSynth::_start(const Event &p_event, int p_ground, float p_weight) {
	const GroundInfo &gi = ground_info(p_ground);
	const FellowShoeInfo &si = shoe_info(p_event.shoe);
	const float part = p_event.part == PART_HEEL ? 1.0f : (p_event.part == PART_BALL ? 0.55f : 0.3f);
	const float speed = CLAMP(p_event.speed, 0.05f, 6.0f);
	const float amp = FOOT_LEVEL * gi.level * Math::sqrt(MAX(p_event.load, 1.0f) / 75.0f) * (speed / 0.5f) * part * p_weight;
	if (p_event.part == PART_LIFT) {
		// The toe leaving: loose ground kicked (a few grains), mud sucking, water dripping.
		if (p_ground == GROUND_MUD) {
			_bubble(p_event.delay + int(0.02f * rate), 0.004f + 0.003f * rng.randf(), amp * 6.0f, 3.0f);
			stats.bubbles++;
		} else if (p_ground == GROUND_WATER) {
			const int n = 2 + int(rng.rand() % 3);
			for (int i = 0; i < n; i++) {
				_bubble(p_event.delay + int((0.05f + 0.3f * rng.randf()) * rate), 0.0006f + 0.0009f * rng.randf(), amp * 0.6f, 0.1f);
				stats.bubbles++;
			}
			return;
		} else if (!gi.loose) {
			return;
		}
	}
	if (strikes.size() >= 16) {
		return;
	}
	Strike s;
	s.ground = uint8_t(p_ground);
	s.shoe = p_event.shoe;
	s.part = p_event.part;
	s.amp = amp;
	s.delay = MAX(0, p_event.delay);
	s.nres = gi.resonances;
	for (int i = 0; i < gi.resonances; i++) {
		_tune(s.res[i], gi.frequency[i] * (1.0f + 0.04f * (rng.randf() - 0.5f)), gi.q[i], gi.gain[i]);
	}
	// The sole compresses longer on soft ground (the ground giving as well) and for a slower foot (Hertz: v^-1/5).
	const float tau = si.tau * Math::pow(speed / 0.5f, -0.2f) * (gi.loose ? 2.0f : 1.0f);
	s.length = MAX(1, int(tau * rate));
	_tune(s.shoe_res, si.frequency, si.q, si.gain * 4.0f);
	// The click: the force's rate of change radiating (a dipole), stronger the shorter the contact.
	s.click = amp * 0.6f * (0.001f / tau) * si.click * MAX(gi.hardness, 0.2f);
	s.thump_env = amp * gi.thump;
	s.thump_decay = Math::exp(-1.0f / (0.04f * rate));
	s.thump_lp1.set(gi.thump_hz, rate);
	s.thump_lp2.a = s.thump_lp1.a;
	s.energy = gi.loose ? amp : 0.0f;
	s.energy_decay = Math::exp(-1.0f / (MAX(gi.press, 0.02f) * rate));
	s.hiss_env = amp * gi.hiss;
	s.hiss_decay = Math::exp(-1.0f / (0.12f * rate));
	s.hiss_hp.set(3000.0f, rate);
	s.hiss_lp.set(5000.0f, rate);
	float ring = 0.0f;
	for (int i = 0; i < gi.resonances; i++) {
		ring = MAX(ring, 7.0f * gi.q[i] / (float(Math::PI) * gi.frequency[i]));
	}
	s.life = int((MAX(gi.press * 7.0f, 0.15f) + ring) * rate) + s.delay;
	if (p_ground == GROUND_MUD) {
		s.squelch = 1.0f;
	}
	if (p_ground == GROUND_WATER && p_event.part != PART_LIFT) {
		// A splash: the foot's slap on the water, bubbles entrained (radii 0.8-4 mm) over its first 80 ms.
		s.hiss_env = amp * 1.5f;
		s.hiss_decay = Math::exp(-1.0f / (0.06f * rate));
		s.hiss_hp.set(900.0f, rate);
		s.hiss_lp.set(7000.0f, rate);
		const int n = 6 + int(rng.rand() % 8);
		for (int i = 0; i < n; i++) {
			_bubble(s.delay + int(0.08f * rng.randf() * rate), 0.0008f + 0.0032f * rng.randf(), amp * (0.2f + 0.4f * rng.randf()), 0.1f);
			stats.bubbles++;
		}
	}
	strikes.push_back(s);
	stats.strikes++;
}

void FellowFootstepSynth::render(float *r_left, float *r_right, int p_frames) {
	{
		MutexLock lock(mutex);
		for (const Event &e : posted) {
			for (int g = 0; g < 2; g++) {
				if (e.weight[g] > 0.15f) {
					_start(e, e.ground[g], e.weight[g]);
				}
			}
		}
		posted.clear();
		scuff = posted_scuff;
	}
	float peak = 0.0f;
	uint64_t grains = 0;
	const float dt = 1.0f / rate;
	for (int b0 = 0; b0 < p_frames; b0 += FOOT_BLOCK) {
		const int n = MIN(FOOT_BLOCK, p_frames - b0);
		float *L = r_left + b0;
		for (int i = 0; i < n; i++) {
			L[i] = 0.0f;
		}
		// The strikes.
		for (uint32_t k = 0; k < strikes.size();) {
			Strike &s = strikes[k];
			const GroundInfo &gi = ground_info(s.ground);
			const float kp = float(Math::PI) / float(s.length);
			const float grain_p = gi.grain_rate * dt / FOOT_LEVEL;
			for (int i = 0; i < n; i++) {
				if (s.delay > 0) {
					s.delay--;
					continue;
				}
				// The force: a half-sine over the sole's compression (its integral the strike's strength).
				float force = 0.0f;
				if (s.pos < s.length) {
					force = s.amp * 0.5f * kp * Math::sin((float(s.pos) + 0.5f) * kp);
				}
				s.pos++;
				float x = 0.0f;
				float drive = gi.loose ? 0.0f : force * gi.hardness;
				// Loose ground: the energy left crushes grains, each a click through the material's resonances.
				if (gi.loose && s.energy > s.amp * 0.003f) { // (to -50 dB: the step done)
					if (rng.randf() < grain_p * s.energy) {
						const float g = MIN(20.0f, Math::pow(1.0f - 0.999f * rng.randf(), -1.0f / gi.grain_alpha));
						drive += (rng.randf() < 0.5f ? -1.0f : 1.0f) * s.energy * gi.grain_level * g * 0.09f;
						grains++;
					}
					s.energy *= s.energy_decay;
				}
				for (int r = 0; r < s.nres; r++) {
					Res &rs = s.res[r];
					const float y = rs.a1 * rs.y1 - rs.a2 * rs.y2 + rs.b * drive;
					rs.y2 = rs.y1;
					rs.y1 = y;
					x += rs.gain * y;
				}
				// The shoe's own resonance and its click (the force's rate of change, a dipole), on hard ground.
				if (!gi.loose || s.ground == GROUND_DIRT) {
					Res &rs = s.shoe_res;
					const float y = rs.a1 * rs.y1 - rs.a2 * rs.y2 + rs.b * force;
					rs.y2 = rs.y1;
					rs.y1 = y;
					x += rs.gain * y * (gi.loose ? 0.3f : MIN(MAX(gi.hardness, 0.2f), 1.0f));
					if (s.pos <= s.length) {
						x += s.click * Math::cos((float(s.pos) - 0.5f) * kp) * (gi.loose ? 0.3f : 1.0f);
					}
				}
				s.prev_force = force;
				// The ground's compression: a low thump.
				if (s.thump_env > s.amp * 1e-3f) {
					x += s.thump_env * 4.0f * s.thump_lp2.lp(s.thump_lp1.lp(_noise()));
					s.thump_env *= s.thump_decay;
				}
				// Brushing (grass, leaves) and a splash's slap.
				if (s.hiss_env > s.amp * 1e-3f) {
					x += s.hiss_env * s.hiss_lp.lp(s.hiss_hp.hp(_noise()));
					s.hiss_env *= s.hiss_decay;
				}
				// Mud: the foot sinking, a squelch rising in pitch.
				if (s.squelch > 1e-4f) {
					const float fc = 150.0f + 1350.0f * (1.0f - s.squelch);
					const float a = 1.0f - Math::exp(-float(Math::TAU) * fc / rate);
					s.squelch_y += a * (_noise() - s.squelch_y);
					x += s.amp * 2.5f * s.squelch * s.squelch_y;
					s.squelch *= Math::exp(-1.0f / (0.15f * rate));
				}
				L[i] += x;
			}
			for (int r = 0; r < s.nres; r++) {
				if (Math::abs(s.res[r].y1) + Math::abs(s.res[r].y2) < 1e-15f) {
					s.res[r].y1 = 0.0f; // no denormals once a resonance has died away
					s.res[r].y2 = 0.0f;
				}
			}
			if (Math::abs(s.shoe_res.y1) + Math::abs(s.shoe_res.y2) < 1e-15f) {
				s.shoe_res.y1 = 0.0f;
				s.shoe_res.y2 = 0.0f;
			}
			s.life -= n;
			if (s.life <= 0) {
				strikes.remove_at_unordered(k);
				continue;
			}
			k++;
		}
		// Bubbles (mud's suction, water's splash and drips): decision 0319's Minnaert bubbles.
		for (uint32_t k = 0; k < bubbles.size();) {
			Bubble &b = bubbles[k];
			for (int i = 0; i < n; i++) {
				if (b.delay > 0) {
					b.delay--;
					continue;
				}
				const float f = b.f0 * MIN(1.6f, 1.0f + b.rise * b.damping * b.t);
				b.phase += float(Math::TAU) * f * dt;
				L[i] += b.amp * Math::exp(-b.damping * b.t) * Math::sin(b.phase) * MIN(1.0f, b.t * rate / 8.0f);
				b.t += dt;
			}
			if (b.delay <= 0 && b.damping * b.t > 9.0f) {
				bubbles.remove_at_unordered(k);
				continue;
			}
			k++;
		}
		// A foot sliding while down: grains rolling under it, or a rub (and a sneaker's squeak on a smooth floor).
		if (scuff.speed > 0.05f) {
			const GroundInfo &gi = ground_info(scuff.ground);
			if (scuff_ground != int(scuff.ground)) {
				scuff_ground = scuff.ground;
				for (int r = 0; r < 3; r++) {
					if (r < gi.resonances) {
						_tune(scuff_res[r], gi.frequency[r], gi.q[r], gi.gain[r]);
					} else {
						scuff_res[r] = Res();
					}
				}
				scuff_hp.set(1500.0f, rate);
				scuff_lp.set(6000.0f, rate);
			}
			const float load = Math::sqrt(MAX(scuff.load, 1.0f) / 75.0f);
			const float level = FOOT_LEVEL * load * MIN(scuff.speed, 3.0f);
			const bool squeaky = scuff.shoe == SHOE_SNEAKER && (scuff.ground == GROUND_WOOD || scuff.ground == GROUND_TILE);
			const float squeak_target = squeaky ? CLAMP((scuff.speed - 0.5f) / 0.6f, 0.0f, 1.0f) : 0.0f;
			if (squeak_target > 0.0f && squeak_env < 0.01f) {
				squeak_f = 1300.0f + 1000.0f * rng.randf();
			}
			for (int i = 0; i < n; i++) {
				float drive;
				if (gi.loose) {
					drive = 0.0f;
					if (rng.randf() < gi.grain_rate * 0.5f * MIN(scuff.speed, 3.0f) * dt) {
						drive = (rng.randf() < 0.5f ? -1.0f : 1.0f) * level * gi.grain_level * 0.03f * MIN(20.0f, Math::pow(1.0f - 0.999f * rng.randf(), -1.0f / gi.grain_alpha));
						grains++;
					}
				} else {
					drive = level * 0.02f * _noise();
				}
				float x = 0.0f;
				for (int r = 0; r < 3; r++) {
					Res &rs = scuff_res[r];
					const float y = rs.a1 * rs.y1 - rs.a2 * rs.y2 + rs.b * drive;
					rs.y2 = rs.y1;
					rs.y1 = y;
					x += rs.gain * y;
				}
				if (!gi.loose) {
					x += level * 0.08f * scuff_lp.lp(scuff_hp.hp(_noise())) * gi.hardness;
				}
				squeak_env += (squeak_target - squeak_env) * 0.002f;
				if (squeak_env > 1e-3f) {
					vibrato += float(Math::TAU) * 35.0f * dt;
					squeak_phase += float(Math::TAU) * squeak_f * (1.0f + 0.02f * Math::sin(vibrato)) * dt;
					x += FOOT_LEVEL * 0.15f * squeak_env * (Math::sin(squeak_phase) + 0.4f * Math::sin(2.0f * squeak_phase) + 0.15f * Math::sin(3.0f * squeak_phase));
				}
				L[i] += x;
			}
		} else {
			squeak_env = 0.0f;
		}
		for (int i = 0; i < n; i++) {
			peak = MAX(peak, Math::abs(L[i]));
			L[i] = foot_soft_limit(L[i]);
			r_right[b0 + i] = L[i];
		}
	}
	MutexLock lock(mutex);
	stats.grains += grains;
	stats.peak = MAX(stats.peak, peak);
	stats.quiet = strikes.is_empty() && bubbles.is_empty() && posted.is_empty() && posted_scuff.speed <= 0.05f;
}

// The node.

int FellowFootsteps3D::ground_from_name(const String &p_name) {
	const String n = p_name.to_lower().strip_edges();
	for (int g = 0; g < GROUND_MAX; g++) {
		if (n == FellowFootstepSynth::ground_info(g).name) {
			return g;
		}
	}
	if (n == "concrete" || n == "rock" || n == "asphalt") {
		return GROUND_STONE;
	}
	if (n == "ice" || n == "glass" || n == "marble") {
		return GROUND_TILE;
	}
	if (n == "earth" || n == "soil") {
		return GROUND_DIRT;
	}
	if (n == "puddle" || n == "shallow water") {
		return GROUND_WATER;
	}
	return -1;
}

void FellowFootsteps3D::_find_bones() {
	bones_valid = false;
	Skeleton3D *sk = get_skeleton();
	if (!sk) {
		return;
	}
	bones_valid = true;
	// The ground at rest: the skeleton's origin, or its feet's lowest joint when they rest well below it (a mocap rig
	// on its hips, as FellowFootLock3D finds it).
	float rest_ground = 0.0f;
	for (int s = 0; s < 2; s++) {
		const int fb = sk->find_bone(foot_bones[s]);
		const int tb = toe_bones[s] == StringName() ? -1 : sk->find_bone(toe_bones[s]);
		if (fb >= 0) {
			rest_ground = MIN(rest_ground, (float)sk->get_bone_global_rest(fb).origin.y);
		}
		if (tb >= 0) {
			rest_ground = MIN(rest_ground, (float)sk->get_bone_global_rest(tb).origin.y);
		}
	}
	if (rest_ground > -0.1f) {
		rest_ground = 0.0f;
	}
	for (int s = 0; s < 2; s++) {
		Foot &f = feet[s];
		f.foot = sk->find_bone(foot_bones[s]);
		f.toe = toe_bones[s] == StringName() ? -1 : sk->find_bone(toe_bones[s]);
		if (f.foot < 0) {
			bones_valid = false;
			continue;
		}
		const Transform3D foot_rest = sk->get_bone_global_rest(f.foot);
		const Vector3 ankle = foot_rest.origin;
		const Vector3 toe = f.toe >= 0 ? sk->get_bone_global_rest(f.toe).origin : ankle;
		Vector3 forward = Vector3(toe.x - ankle.x, 0.0f, toe.z - ankle.z);
		forward = forward.length() > 1e-4f ? forward.normalized() : Vector3();
		const Vector3 heel = Vector3(ankle.x, rest_ground, ankle.z) - forward * heel_offset;
		f.heel_local = foot_rest.affine_inverse().xform(heel);
		f.ball_local = foot_rest.affine_inverse().xform(Vector3(toe.x, rest_ground, toe.z));
		f.has_last = false;
		f.heel = Point();
		f.ball = Point();
	}
}

void FellowFootsteps3D::_skeleton_changed(Skeleton3D *p_old, Skeleton3D *p_new) {
	SkeletonModifier3D::_skeleton_changed(p_old, p_new);
	_find_bones();
}

// What the foot stands on: a ray down through it (not the character's own body); the collider's ground — its
// FellowContactSound3D's surface or material, its "fellow_surface" metadata, a terrain's layers there by their weights
// (the two strongest) — or default_ground; water over the ground (wading): water.
void FellowFootsteps3D::_ground_under(Foot &r_foot, const Vector3 &p_at, const Vector3 &p_up) {
	r_foot.ground_hit = false;
	r_foot.ground[0] = uint8_t(default_ground);
	r_foot.weight[0] = 1.0f;
	r_foot.weight[1] = 0.0f;
	Skeleton3D *sk = get_skeleton();
	if (!sk || !sk->is_inside_tree() || sk->get_world_3d().is_null()) {
		return;
	}
	PhysicsDirectSpaceState3D *space = PhysicsServer3D::get_singleton()->space_get_direct_state(sk->get_world_3d()->get_space());
	if (!space) {
		return;
	}
	PhysicsDirectSpaceState3D::RayParameters ray;
	ray.from = p_at + p_up * 0.5f;
	ray.to = p_at - p_up * 0.7f;
	ray.collision_mask = collision_mask;
	HashSet<RID> exclude;
	for (Node *p = sk; p; p = p->get_parent()) {
		if (CollisionObject3D *co = Object::cast_to<CollisionObject3D>(p)) {
			exclude.insert(co->get_rid());
		}
	}
	ray.exclude = exclude;
	PhysicsDirectSpaceState3D::RayResult hit;
	if (!space->intersect_ray(ray, hit)) {
		return;
	}
	r_foot.ground_hit = true;
	r_foot.ground_y = hit.position.y;
	r_foot.ground_point = hit.position;
	int ground = -1;
	Object *collider = hit.collider;
	if (collider) {
		const ObjectID id = collider->get_instance_id();
		if (const int *cached = ground_cache.getptr(id)) {
			ground = *cached;
		} else {
			if (Node *node = Object::cast_to<Node>(collider)) {
				for (int i = 0; i < node->get_child_count() && ground < 0; i++) {
					if (const FellowContactSound3D *cs = Object::cast_to<FellowContactSound3D>(node->get_child(i))) {
						ground = cs->get_ground();
					}
				}
			}
			if (ground < 0 && collider->has_meta(SNAME("fellow_surface"))) {
				const Variant v = collider->get_meta(SNAME("fellow_surface"));
				ground = v.get_type() == Variant::STRING || v.get_type() == Variant::STRING_NAME ? ground_from_name(v) : CLAMP(int(v), -1, int(GROUND_MAX) - 1);
			}
			const bool terrain = ground < 0 && collider->has_method(SNAME("get_layer_weights"));
			if (!terrain) {
				if (ground_cache.size() > 128) {
					ground_cache.clear();
				}
				ground_cache[id] = ground;
			} else {
				// A terrain: its layers' grounds by their weights here.
				const PackedFloat32Array w = collider->call(SNAME("get_layer_weights"), hit.position);
				const Array layers = collider->get(SNAME("layers"));
				float sum[GROUND_MAX] = {};
				for (int i = 0; i < w.size() && i < layers.size(); i++) {
					const Object *layer = layers[i];
					bool valid = false;
					const Variant gv = layer ? layer->get(SNAME("surface"), &valid) : Variant();
					const int g = valid ? CLAMP(int(gv), 0, int(GROUND_MAX) - 1) : int(GROUND_DIRT);
					sum[g] += MAX(0.0f, w[i]);
				}
				int a = -1;
				int b = -1;
				for (int g = 0; g < GROUND_MAX; g++) {
					if (a < 0 || sum[g] > sum[a]) {
						b = a;
						a = g;
					} else if (b < 0 || sum[g] > sum[b]) {
						b = g;
					}
				}
				const float total = (a >= 0 ? sum[a] : 0.0f) + (b >= 0 ? sum[b] : 0.0f);
				if (a >= 0 && total > 0.0f) {
					r_foot.ground[0] = uint8_t(a);
					r_foot.weight[0] = sum[a] / total;
					r_foot.ground[1] = uint8_t(b >= 0 ? b : a);
					r_foot.weight[1] = b >= 0 ? sum[b] / total : 0.0f;
					ground = -2;
				}
			}
		}
	}
	if (ground >= 0) {
		r_foot.ground[0] = uint8_t(ground);
		r_foot.weight[0] = 1.0f;
		r_foot.weight[1] = 0.0f;
	}
	// Wading: water over the ground here.
	if (FellowWater3D::has_instances()) {
		if (FellowWater3D *water = FellowWater3D::find_water(hit.position)) {
			if (water->get_surface_height(hit.position) > hit.position.y + 0.02f) {
				r_foot.ground[0] = GROUND_WATER;
				r_foot.weight[0] = 1.0f;
				r_foot.weight[1] = 0.0f;
			}
		}
	}
}

void FellowFootsteps3D::_emit(int p_foot, int p_part, const Vector3 &p_at, float p_speed) {
	Foot &f = feet[p_foot];
	if (p_part == FellowFootstepSynth::PART_HEEL) {
		steps++;
		emit_signal(SNAME("footstep"), p_foot, p_at, int(f.ground[0]), p_speed);
	} else if (p_part == FellowFootstepSynth::PART_LIFT) {
		lifts++;
	}
	last_ground = f.ground[0];
	if (!sound_enabled) {
		return;
	}
	const Node3D *listener = fellow_sound_listener(this);
	if (listener && listener->get_global_position().distance_to(p_at) > sound_max_distance * 1.1f) {
		return;
	}
	FellowFootstepSynth *synth = (FellowFootstepSynth *)voice.get_source();
	if (!synth) {
		synth = (FellowFootstepSynth *)voice.start_3d(this, &FellowFootstepSynth::make, sound_bus, sound_volume_db, sound_unit_size, sound_max_distance);
	}
	if (!synth) {
		return;
	}
	FellowFootstepSynth::Event e;
	e.ground[0] = f.ground[0];
	e.ground[1] = f.ground[1];
	e.weight[0] = f.weight[0];
	e.weight[1] = f.weight[1];
	e.part = uint8_t(p_part);
	e.shoe = uint8_t(shoe);
	e.speed = p_speed;
	e.load = body_mass;
	synth->event(e);
	voice.place(p_at);
	quiet_for = 0.0;
}

// Each pass: every foot's heel and ball (as drawn) against the ground under the foot. A point coming within
// contact_height of it and planted (slower than 0.6 m/s across the ground) is a touch — the first of the foot a strike, the second the sole rolling onto it — at the
// speed it came down; the ball leaving after the heel is the toe lifting; a foot moving across the ground while down
// scuffs. Only when the pose has changed (the animation may run slower than the frames).
void FellowFootsteps3D::_process_modification(double p_delta) {
	Skeleton3D *sk = get_skeleton();
	if (!sk) {
		return;
	}
	if (!bones_valid) {
		_find_bones();
		if (!bones_valid) {
			return;
		}
	}
	const float dt = float(MAX(p_delta, 0.0));
	clock += dt;
	const Transform3D xform = sk->get_global_transform_interpolated();
	const Vector3 up = Vector3(0.0f, 1.0f, 0.0f);
	float best_scuff = 0.0f;
	int scuff_foot = -1;
	Vector3 mid;
	for (int s = 0; s < 2; s++) {
		Foot &f = feet[s];
		const Transform3D fx = xform * sk->get_bone_global_pose(f.foot);
		const Vector3 ankle = fx.origin;
		const Vector3 heel = fx.xform(f.heel_local);
		const Vector3 ball = f.toe >= 0 ? fx.xform(f.ball_local) : heel;
		mid += (heel + ball) * 0.25f;
		f.since_pose += dt;
		if (f.has_last && ankle.is_equal_approx(f.last_ankle)) {
			continue;
		}
		const float pdt = f.has_last ? MAX(f.since_pose, 1e-3f) : dt;
		f.since_pose = 0.0f;
		_ground_under(f, (heel + ball) * 0.5f, up);
		const float ground = f.ground_hit && Math::abs(f.ground_y - xform.origin.y) < 0.6f ? f.ground_y : float(xform.origin.y);
		// Each point's height over the ground, how fast it came down lately (over the last 0.1 s), its sweep.
		Point *pts[2] = { &f.heel, &f.ball };
		const Vector3 pos[2] = { heel, ball };
		const float keep = Math::exp(-pdt / 0.1f);
		for (int k = 0; k < 2; k++) {
			Point &p = *pts[k];
			const float h = float(pos[k].y) - ground;
			const float v_down = f.has_last ? (p.height - h) / pdt : 0.0f;
			Vector3 hv = f.has_last ? (pos[k] - p.position) / pdt : Vector3();
			hv.y = 0.0f;
			p.sweep = float(hv.length());
			p.approach = MAX(p.approach * keep, v_down);
			p.height = h;
			p.position = pos[k];
		}
		// The foot down: its lower point within contact_height and planted (slower than 0.6 m/s across the ground:
		// a swinging foot can pass as low, not as still); up again when both are raised or swept away.
		const int lower = f.ball.height < f.heel.height ? 1 : 0;
		const Point &lp = *pts[lower];
		const bool touch = lp.height < contact_height && (!f.has_last || lp.sweep < 0.6f);
		const bool leave = MIN(f.heel.height, f.ball.height) > contact_height + 0.02f || MIN(f.heel.sweep, f.ball.sweep) > 1.2f;
		if (!f.down && touch) {
			f.down = true;
			f.down_since = clock;
			f.rolled = false;
			if (f.has_last && clock - f.last_event > 0.15) {
				f.last_event = clock;
				// The first touch, at the speed the point came down (and a little of its sweep).
				_emit(s, FellowFootstepSynth::PART_HEEL, pos[lower], MIN(MAX(lp.approach, 0.05f) + 0.15f * lp.sweep, 6.0f));
				pts[lower]->approach = 0.0f;
			}
		} else if (f.down && leave) {
			f.down = false;
			if (f.has_last && clock - f.down_since > 0.1) { // (not a flicker)
				const Point &up_point = *pts[f.toe >= 0 ? 1 : 0];
				_emit(s, FellowFootstepSynth::PART_LIFT, pos[f.toe >= 0 ? 1 : 0], MIN(MAX(-up_point.approach, 0.05f) + 0.1f, 6.0f));
			}
		} else if (f.down && !f.rolled && f.toe >= 0 && pts[1 - lower]->height < contact_height && clock - f.last_event > 0.03) {
			// The sole rolling down onto its other point.
			f.rolled = true;
			_emit(s, FellowFootstepSynth::PART_BALL, pos[1 - lower], MIN(MAX(pts[1 - lower]->approach, 0.05f), 6.0f));
			pts[1 - lower]->approach = 0.0f;
		}
		if (f.down && f.has_last) {
			const float sp = MIN(f.heel.sweep, f.ball.sweep);
			if (sp > best_scuff) {
				best_scuff = sp;
				scuff_foot = s;
			}
		}
		f.has_last = true;
		f.last_ankle = ankle;
	}
	FellowFootstepSynth *synth = (FellowFootstepSynth *)voice.get_source();
	if (synth) {
		FellowFootstepSynth::Scuff sc;
		if (scuff_foot >= 0 && best_scuff > 0.25f) {
			sc.ground = feet[scuff_foot].ground[0];
			sc.shoe = uint8_t(shoe);
			sc.speed = best_scuff;
			sc.load = body_mass;
			scuff_seconds += dt;
		}
		synth->set_scuff(sc);
		voice.place(mid);
		const bool quiet = synth->get_stats().quiet;
		quiet_for = quiet ? quiet_for + dt : 0.0;
		const Node3D *listener = fellow_sound_listener(this);
		const bool far = listener && listener->get_global_position().distance_to(mid) > sound_max_distance * 1.1f;
		if (quiet_for > 3.0 || far || !sound_enabled) {
			voice.stop();
		}
	}
}

void FellowFootsteps3D::_notification(int p_what) {
	if (p_what == NOTIFICATION_EXIT_TREE) {
		voice.stop();
		ground_cache.clear();
	}
}

void FellowFootsteps3D::set_left_foot_bone(const StringName &p_name) {
	foot_bones[0] = p_name;
	_find_bones();
}

void FellowFootsteps3D::set_right_foot_bone(const StringName &p_name) {
	foot_bones[1] = p_name;
	_find_bones();
}

void FellowFootsteps3D::set_left_toe_bone(const StringName &p_name) {
	toe_bones[0] = p_name;
	_find_bones();
}

void FellowFootsteps3D::set_right_toe_bone(const StringName &p_name) {
	toe_bones[1] = p_name;
	_find_bones();
}

void FellowFootsteps3D::set_heel_offset(float p_offset) {
	heel_offset = MAX(0.0f, p_offset);
	_find_bones();
}

void FellowFootsteps3D::set_sound_enabled(bool p_enabled) {
	sound_enabled = p_enabled;
	if (!sound_enabled) {
		voice.stop();
	}
}

void FellowFootsteps3D::set_sound_volume_db(float p_db) {
	sound_volume_db = p_db;
	voice.set_volume_db(p_db);
}

void FellowFootsteps3D::set_sound_bus(const StringName &p_bus) {
	sound_bus = p_bus;
	voice.set_bus(p_bus);
}

void FellowFootsteps3D::set_sound_unit_size(float p_size) {
	sound_unit_size = MAX(0.1f, p_size);
	voice.set_unit_size(sound_unit_size);
}

void FellowFootsteps3D::set_sound_max_distance(float p_distance) {
	sound_max_distance = MAX(1.0f, p_distance);
	voice.set_max_distance(sound_max_distance);
}

int FellowFootsteps3D::get_ground(int p_foot) const {
	ERR_FAIL_INDEX_V(p_foot, 2, -1);
	return feet[p_foot].ground[0];
}

// A foot as last seen: down or not, its heel's and ball's heights over the ground under it and their speeds across it.
Dictionary FellowFootsteps3D::get_foot_state(int p_foot) const {
	ERR_FAIL_INDEX_V(p_foot, 2, Dictionary());
	const Foot &f = feet[p_foot];
	Dictionary d;
	d["down"] = f.down;
	d["heel_height"] = f.heel.height;
	d["ball_height"] = f.ball.height;
	d["heel_sweep"] = f.heel.sweep;
	d["ball_sweep"] = f.ball.sweep;
	d["ground"] = int(f.ground[0]);
	d["ground2"] = int(f.ground[1]);
	d["weight2"] = f.weight[1];
	return d;
}

Dictionary FellowFootsteps3D::get_sound_stats() {
	Dictionary d;
	uint64_t frames = 0, usec = 0;
	voice.get_mix_stats(frames, usec);
	const float rate = AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 48000.0f;
	d["playing"] = voice.is_playing();
	d["steps"] = (int64_t)steps;
	d["lifts"] = (int64_t)lifts;
	d["scuff_seconds"] = scuff_seconds;
	d["ground"] = last_ground;
	if (FellowFootstepSynth *synth = (FellowFootstepSynth *)voice.get_source()) {
		const FellowFootstepSynth::Stats st = synth->get_stats();
		d["strikes"] = (int64_t)st.strikes;
		d["grains"] = (int64_t)st.grains;
		d["bubbles"] = (int64_t)st.bubbles;
		d["peak"] = st.peak;
	}
	d["mixed_seconds"] = double(frames) / rate;
	d["mix_usec_per_second"] = frames > 0 ? double(usec) / (double(frames) / rate) : 0.0;
	return d;
}

// Footsteps offline (tests): events by time, each a Dictionary {time, part: "heel", "ball" or "lift", ground (a
// Ground, or a name), ground2 and weight2 (a second ground and its share), shoe, speed (m/s), load (kg)} or {time,
// part: "scuff", ground, shoe, speed} (speed 0 ends it). The samples (interleaved stereo, the same in both), the
// counts and the synthesis's time a second of sound.
Dictionary FellowFootsteps3D::debug_render(const Array &p_events, float p_seconds, float p_rate, int p_seed) {
	FellowFootstepSynth synth;
	synth.set_rate(p_rate);
	synth.set_seed(uint64_t(p_seed) * 0x9e3779b97f4a7c15ull + 3);
	PackedFloat32Array out;
	const int total = int(MAX(0.0f, p_seconds) * p_rate);
	out.resize(total * 2);
	float *w = out.ptrw();
	LocalVector<float> l, r;
	int pos = 0;
	uint64_t usec = 0;
	for (int k = 0; k <= p_events.size() && pos < total; k++) {
		const Dictionary ev = k < p_events.size() ? Dictionary(p_events[k]) : Dictionary();
		const int at = k < p_events.size() ? CLAMP(int(float(ev.get("time", 0.0)) * p_rate), pos, total) : total;
		while (pos < at) {
			const int n = MIN(1024, at - pos);
			l.resize(n);
			r.resize(n);
			const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
			synth.render(l.ptr(), r.ptr(), n);
			usec += OS::get_singleton()->get_ticks_usec() - t0;
			for (int i = 0; i < n; i++) {
				w[(pos + i) * 2] = l[i];
				w[(pos + i) * 2 + 1] = r[i];
			}
			pos += n;
		}
		if (k >= p_events.size()) {
			break;
		}
		auto ground_of = [](const Variant &p_v) {
			const int g = p_v.get_type() == Variant::STRING || p_v.get_type() == Variant::STRING_NAME ? ground_from_name(p_v) : int(p_v);
			return CLAMP(g, 0, int(GROUND_MAX) - 1);
		};
		const String part = ev.get("part", "heel");
		if (part == "scuff") {
			FellowFootstepSynth::Scuff sc;
			sc.ground = uint8_t(ground_of(ev.get("ground", 0)));
			sc.shoe = uint8_t(CLAMP(int(ev.get("shoe", 0)), 0, int(FellowFootstepSynth::SHOE_MAX) - 1));
			sc.speed = ev.get("speed", 0.0f);
			sc.load = ev.get("load", 75.0f);
			synth.set_scuff(sc);
			continue;
		}
		FellowFootstepSynth::Event e;
		e.ground[0] = uint8_t(ground_of(ev.get("ground", 0)));
		e.weight[0] = 1.0f;
		if (ev.has("ground2")) {
			e.ground[1] = uint8_t(ground_of(ev["ground2"]));
			e.weight[1] = CLAMP(float(ev.get("weight2", 0.5f)), 0.0f, 1.0f);
			e.weight[0] = 1.0f - e.weight[1];
		}
		e.part = uint8_t(part == "ball" ? FellowFootstepSynth::PART_BALL : (part == "lift" ? FellowFootstepSynth::PART_LIFT : FellowFootstepSynth::PART_HEEL));
		e.shoe = uint8_t(CLAMP(int(ev.get("shoe", 0)), 0, int(FellowFootstepSynth::SHOE_MAX) - 1));
		e.speed = ev.get("speed", 0.5f);
		e.load = ev.get("load", 75.0f);
		synth.event(e);
	}
	const FellowFootstepSynth::Stats st = synth.get_stats();
	Dictionary d;
	d["samples"] = out;
	d["strikes"] = (int64_t)st.strikes;
	d["grains"] = (int64_t)st.grains;
	d["bubbles"] = (int64_t)st.bubbles;
	d["usec_per_second"] = total > 0 ? double(usec) / (double(total) / p_rate) : 0.0;
	return d;
}

PackedStringArray FellowFootsteps3D::get_configuration_warnings() const {
	PackedStringArray warnings = SkeletonModifier3D::get_configuration_warnings();
	const Skeleton3D *sk = get_skeleton();
	if (sk) {
		for (int s = 0; s < 2; s++) {
			if (sk->find_bone(foot_bones[s]) < 0) {
				warnings.push_back(vformat(RTR("The skeleton has no bone \"%s\" (a foot)."), foot_bones[s]));
			}
		}
	}
	return warnings;
}

void FellowFootsteps3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_left_foot_bone", "bone"), &FellowFootsteps3D::set_left_foot_bone);
	ClassDB::bind_method(D_METHOD("get_left_foot_bone"), &FellowFootsteps3D::get_left_foot_bone);
	ClassDB::bind_method(D_METHOD("set_right_foot_bone", "bone"), &FellowFootsteps3D::set_right_foot_bone);
	ClassDB::bind_method(D_METHOD("get_right_foot_bone"), &FellowFootsteps3D::get_right_foot_bone);
	ClassDB::bind_method(D_METHOD("set_left_toe_bone", "bone"), &FellowFootsteps3D::set_left_toe_bone);
	ClassDB::bind_method(D_METHOD("get_left_toe_bone"), &FellowFootsteps3D::get_left_toe_bone);
	ClassDB::bind_method(D_METHOD("set_right_toe_bone", "bone"), &FellowFootsteps3D::set_right_toe_bone);
	ClassDB::bind_method(D_METHOD("get_right_toe_bone"), &FellowFootsteps3D::get_right_toe_bone);
	ClassDB::bind_method(D_METHOD("set_contact_height", "metres"), &FellowFootsteps3D::set_contact_height);
	ClassDB::bind_method(D_METHOD("get_contact_height"), &FellowFootsteps3D::get_contact_height);
	ClassDB::bind_method(D_METHOD("set_heel_offset", "metres"), &FellowFootsteps3D::set_heel_offset);
	ClassDB::bind_method(D_METHOD("get_heel_offset"), &FellowFootsteps3D::get_heel_offset);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &FellowFootsteps3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &FellowFootsteps3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_body_mass", "kg"), &FellowFootsteps3D::set_body_mass);
	ClassDB::bind_method(D_METHOD("get_body_mass"), &FellowFootsteps3D::get_body_mass);
	ClassDB::bind_method(D_METHOD("set_shoe", "shoe"), &FellowFootsteps3D::set_shoe);
	ClassDB::bind_method(D_METHOD("get_shoe"), &FellowFootsteps3D::get_shoe);
	ClassDB::bind_method(D_METHOD("set_default_ground", "ground"), &FellowFootsteps3D::set_default_ground);
	ClassDB::bind_method(D_METHOD("get_default_ground"), &FellowFootsteps3D::get_default_ground);
	ClassDB::bind_method(D_METHOD("set_sound_enabled", "enabled"), &FellowFootsteps3D::set_sound_enabled);
	ClassDB::bind_method(D_METHOD("is_sound_enabled"), &FellowFootsteps3D::is_sound_enabled);
	ClassDB::bind_method(D_METHOD("set_sound_volume_db", "db"), &FellowFootsteps3D::set_sound_volume_db);
	ClassDB::bind_method(D_METHOD("get_sound_volume_db"), &FellowFootsteps3D::get_sound_volume_db);
	ClassDB::bind_method(D_METHOD("set_sound_bus", "bus"), &FellowFootsteps3D::set_sound_bus);
	ClassDB::bind_method(D_METHOD("get_sound_bus"), &FellowFootsteps3D::get_sound_bus);
	ClassDB::bind_method(D_METHOD("set_sound_unit_size", "metres"), &FellowFootsteps3D::set_sound_unit_size);
	ClassDB::bind_method(D_METHOD("get_sound_unit_size"), &FellowFootsteps3D::get_sound_unit_size);
	ClassDB::bind_method(D_METHOD("set_sound_max_distance", "metres"), &FellowFootsteps3D::set_sound_max_distance);
	ClassDB::bind_method(D_METHOD("get_sound_max_distance"), &FellowFootsteps3D::get_sound_max_distance);
	ClassDB::bind_method(D_METHOD("get_ground", "foot"), &FellowFootsteps3D::get_ground);
	ClassDB::bind_method(D_METHOD("get_foot_state", "foot"), &FellowFootsteps3D::get_foot_state);
	ClassDB::bind_method(D_METHOD("get_sound_stats"), &FellowFootsteps3D::get_sound_stats);
	ClassDB::bind_static_method("FellowFootsteps3D", D_METHOD("debug_render", "events", "seconds", "rate", "seed"), &FellowFootsteps3D::debug_render, DEFVAL(48000.0f), DEFVAL(1));

	ADD_GROUP("Bones", "");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "left_foot_bone"), "set_left_foot_bone", "get_left_foot_bone");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "right_foot_bone"), "set_right_foot_bone", "get_right_foot_bone");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "left_toe_bone"), "set_left_toe_bone", "get_left_toe_bone");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "right_toe_bone"), "set_right_toe_bone", "get_right_toe_bone");
	ADD_GROUP("Contact", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "contact_height", PROPERTY_HINT_RANGE, "0.001,0.2,0.001,suffix:m"), "set_contact_height", "get_contact_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "heel_offset", PROPERTY_HINT_RANGE, "0,0.2,0.001,suffix:m"), "set_heel_offset", "get_heel_offset");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "body_mass", PROPERTY_HINT_RANGE, "1,300,0.5,or_greater,suffix:kg"), "set_body_mass", "get_body_mass");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "shoe", PROPERTY_HINT_ENUM, "Sneaker,Boot,Hard,Barefoot"), "set_shoe", "get_shoe");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "default_ground", PROPERTY_HINT_ENUM, "Stone,Wood,Metal,Tile,Carpet,Gravel,Sand,Snow,Grass,Leaves,Dirt,Mud,Water"), "set_default_ground", "get_default_ground");
	ADD_GROUP("Sound", "sound_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sound_enabled"), "set_sound_enabled", "is_sound_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_volume_db", PROPERTY_HINT_RANGE, "-60,24,0.1,suffix:dB"), "set_sound_volume_db", "get_sound_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sound_bus"), "set_sound_bus", "get_sound_bus");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_unit_size", PROPERTY_HINT_RANGE, "0.1,20,0.1,or_greater,suffix:m"), "set_sound_unit_size", "get_sound_unit_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_max_distance", PROPERTY_HINT_RANGE, "1,500,1,or_greater,suffix:m"), "set_sound_max_distance", "get_sound_max_distance");

	ADD_SIGNAL(MethodInfo("footstep", PropertyInfo(Variant::INT, "foot"), PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::INT, "ground"), PropertyInfo(Variant::FLOAT, "speed")));

	BIND_ENUM_CONSTANT(GROUND_STONE);
	BIND_ENUM_CONSTANT(GROUND_WOOD);
	BIND_ENUM_CONSTANT(GROUND_METAL);
	BIND_ENUM_CONSTANT(GROUND_TILE);
	BIND_ENUM_CONSTANT(GROUND_CARPET);
	BIND_ENUM_CONSTANT(GROUND_GRAVEL);
	BIND_ENUM_CONSTANT(GROUND_SAND);
	BIND_ENUM_CONSTANT(GROUND_SNOW);
	BIND_ENUM_CONSTANT(GROUND_GRASS);
	BIND_ENUM_CONSTANT(GROUND_LEAVES);
	BIND_ENUM_CONSTANT(GROUND_DIRT);
	BIND_ENUM_CONSTANT(GROUND_MUD);
	BIND_ENUM_CONSTANT(GROUND_WATER);
	BIND_ENUM_CONSTANT(GROUND_MAX);
	BIND_ENUM_CONSTANT(SHOE_SNEAKER);
	BIND_ENUM_CONSTANT(SHOE_BOOT);
	BIND_ENUM_CONSTANT(SHOE_HARD);
	BIND_ENUM_CONSTANT(SHOE_BAREFOOT);
}
