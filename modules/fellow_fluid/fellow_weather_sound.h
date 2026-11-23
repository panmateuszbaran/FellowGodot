/**************************************************************************/
/*  fellow_weather_sound.h                                                */
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
#include "core/math/vector3.h"
#include "core/os/mutex.h"
#include "core/templates/local_vector.h"
#include "servers/audio/audio_stream.h"

class AudioStreamPlayer;
class Node;
class Node3D;

// FellowEngine (milestone 178, decision 0320): the weather's sound, synthesised — rain (drops on water, the ground, a
// roof over the listener), wind (its rumble and rush with the gusts, edges whistling, leaves), thunder (a tortuous
// channel's pulses arriving over seconds). Each a stereo synthesis on the audio thread, its parameters posted by its
// node.

// A stereo synthesis (rendered on the audio thread; what the main thread posts is guarded by `mutex`).
class FellowSynthSource {
public:
	Mutex mutex;
	float rate = 48000.0f;
	virtual ~FellowSynthSource() {}
	virtual void set_rate(float p_rate) { rate = MAX(8000.0f, p_rate); }
	// Samples written (not added).
	virtual void render(float *r_left, float *r_right, int p_frames) = 0;
};

class FellowSynthPlayback : public AudioStreamPlayback {
	GDCLASS(FellowSynthPlayback, AudioStreamPlayback);

	FellowSynthSource *source = nullptr;
	bool active = false;
	LocalVector<float> left;
	LocalVector<float> right;
	SafeNumeric<uint64_t> mixed_frames;
	SafeNumeric<uint64_t> mix_usec;

public:
	void set_source(FellowSynthSource *p_source);
	FellowSynthSource *get_source() const { return source; }
	void get_mix_stats(uint64_t &r_frames, uint64_t &r_usec) const;

	virtual void start(double p_from_pos = 0.0) override;
	virtual void stop() override { active = false; }
	virtual bool is_playing() const override { return active; }
	virtual int get_loop_count() const override { return 0; }
	virtual double get_playback_position() const override { return 0.0; }
	virtual void seek(double p_time) override {}
	virtual int mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) override;
	~FellowSynthPlayback();
};

class FellowSynthStream : public AudioStream {
	GDCLASS(FellowSynthStream, AudioStream);

public:
	typedef FellowSynthSource *(*Factory)();

private:
	Factory factory = nullptr;

public:
	void set_factory(Factory p_factory) { factory = p_factory; }
	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual String get_stream_name() const override { return "FellowSynth"; }
	virtual double get_length() const override { return 0.0; }
	virtual bool is_monophonic() const override { return true; }
};

// A node's sound: a player made when first needed, playing its synthesis — heard everywhere (start()), or placed in
// the scene (start_3d(): an AudioStreamPlayer3D, moved with place()).
class FellowAmbientVoice {
	Node *player = nullptr;
	Ref<FellowSynthPlayback> playback;

public:
	// The source (null if the voice could not start).
	FellowSynthSource *start(Node *p_owner, FellowSynthStream::Factory p_factory, const StringName &p_bus, float p_volume_db);
	FellowSynthSource *start_3d(Node *p_owner, FellowSynthStream::Factory p_factory, const StringName &p_bus, float p_volume_db, float p_unit_size, float p_max_distance);
	void place(const Vector3 &p_position);
	void set_unit_size(float p_size);
	void set_max_distance(float p_distance);
	void stop();
	bool is_playing() const;
	void set_volume_db(float p_db);
	void set_bus(const StringName &p_bus);
	void get_mix_stats(uint64_t &r_frames, uint64_t &r_usec) const;
	FellowSynthSource *get_source() const { return playback.is_valid() ? playback->get_source() : nullptr; }
};

// The listener's place: the viewport's AudioListener3D, else its camera (null: none).
Node3D *fellow_sound_listener(Node *p_node);

// Small DSP pieces.
struct FellowSVF {
	float low = 0.0f;
	float band = 0.0f;
	float f = 0.1f;
	float q = 1.0f;
	void set(float p_fc, float p_q, float p_rate);
	_FORCE_INLINE_ float bp(float p_x) {
		low += f * band;
		band += f * (p_x - low - q * band);
		return band;
	}
	_FORCE_INLINE_ float lp(float p_x) {
		bp(p_x);
		return low;
	}
};

struct FellowPink { // Paul Kellet's filter: white noise to pink (-3 dB an octave)
	float b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0;
	_FORCE_INLINE_ float next(float p_white) {
		b0 = 0.99886f * b0 + p_white * 0.0555179f;
		b1 = 0.99332f * b1 + p_white * 0.0750759f;
		b2 = 0.96900f * b2 + p_white * 0.1538520f;
		b3 = 0.86650f * b3 + p_white * 0.3104856f;
		b4 = 0.55000f * b4 + p_white * 0.5329522f;
		b5 = -0.7616f * b5 - p_white * 0.0168980f;
		const float out = b0 + b1 + b2 + b3 + b4 + b5 + b6 + p_white * 0.5362f;
		b6 = p_white * 0.115926f;
		return out * 0.11f;
	}
};

struct FellowOnePole {
	float y = 0.0f;
	float a = 1.0f;
	void set(float p_fc, float p_rate);
	_FORCE_INLINE_ float lp(float p_x) {
		y += a * (p_x - y);
		return y;
	}
	_FORCE_INLINE_ float hp(float p_x) { return p_x - lp(p_x); }
};

// Fire (milestone 179, decision 0321), heard where it burns (one channel: a placed voice pans it):
// * the roar of turbulent combustion — a flame is a monopole source, its sound the rate of change of its heat release
//   (Strahle; Bragg): broadband, peaking lower for a larger fire (about 160 / sqrt(D) Hz), the flames puffing at about
//   1.5 / sqrt(D) Hz (pool fires, Cetegen and Ahmed); a flare's "whoomp" when the fire grows fast (a gas lit);
// * the flames fluttering (a band of noise about 0.6-1.1 kHz, a dozen changes a second) and the hot gas's hiss;
// * the wood's crackle — pockets of steam and gas bursting as it pyrolyses: a snap and a short ring, their energies a
//   power law (crackling noise), often in runs; now and then a pop of sap, lower and longer;
// * the embers' ticks as the char glows and cools.
class FellowFireSynth : public FellowSynthSource {
public:
	struct Params {
		float power = 0.0f; // the flames' heat release over a campfire's (50 kW)
		float size = 0.5f; // m across the fire
		float crackle = 0.0f; // cracks a second (the wood pyrolysing)
		float embers = 0.0f; // glowing char, 0..1
		float gain = 1.0f;
	};
	struct Stats {
		uint64_t cracks = 0;
		uint64_t pops = 0;
		uint64_t ticks = 0;
		uint64_t flares = 0;
	};
	void post(const Params &p_params);
	virtual void render(float *r_left, float *r_right, int p_frames) override;
	Stats get_stats();
	void set_seed(uint64_t p_seed) { rng = RandomPCG(p_seed); }
	static FellowSynthSource *make() { return memnew(FellowFireSynth); }

private:
	struct Crack {
		int delay = 0;
		int snap = 0; // samples of the burst's broadband snap
		float amp = 0.0f;
		float env = 1.0f;
		float decay = 0.9f;
		FellowSVF filter;
	};
	Params target;
	Params cur;
	bool first = true;
	LocalVector<Crack> cracks;
	RandomPCG rng = RandomPCG(0xf17e);
	FellowPink pink;
	FellowOnePole roar_hp1, roar_hp2, roar_lp1, roar_lp2, hiss, hiss_lp, flare_lp1, flare_lp2;
	FellowSVF lick;
	float slow_power = 0.0f; // the power over 0.5 s (a flare: the posted power well over it)
	float puff_phase = 0.0f;
	float flutter = 0.5f;
	float flutter_target = 0.5f;
	float spawn = 0.0f;
	float tick_spawn = 0.0f;
	float flare_amp = 0.0f;
	float flare_t = 1e9f; // s since the flare
	float flare_cooldown = 0.0f;
	Stats stats;
	float _noise() { return rng.randf() * 2.0f - 1.0f; }
	void _crack(int p_delay, float p_amp, float p_fc, float p_q, float p_decay_s, float p_snap_s);
};

// A fire's heat release from its flames' volume (m^3; turbulent flames release about 1.2 MW in each, Orloff and de
// Ris), over a campfire's (50 kW); its size, a flame about 2.5 times as tall as it is wide.
void fellow_fire_from_flames(float p_volume, float &r_power, float &r_size);

// Rain: a bed of distant drops (filtered noise), the near ones each its own — on water a bubble (drops of 0.8-1.1 mm
// entrain one ringing at 13-21 kHz; larger ones lower bubbles) and a tick, on the ground a filtered click — and over a
// sheltered listener the drops on the roof (resonant knocks) with the rain outside muffled.
class FellowRainSynth : public FellowSynthSource {
public:
	struct Params {
		float rain = 0.0f; // 0..1 falling here
		float water = 0.0f; // the share of water around the listener
		float shelter = 0.0f; // the share of the sky above the listener covered
		float roof = 3.0f; // m: the cover's height above the listener
		float gain = 1.0f;
	};
	struct Stats {
		uint64_t drops = 0;
		uint64_t roof_drops = 0;
		uint64_t bubbles = 0;
	};
	void post(const Params &p_params);
	virtual void render(float *r_left, float *r_right, int p_frames) override;
	Stats get_stats();
	static FellowSynthSource *make() { return memnew(FellowRainSynth); }

private:
	struct Drop {
		int delay = 0;
		float gl = 0.0f, gr = 0.0f;
		float env = 0.0f, decay = 0.0f; // the knock: noise through a band
		FellowSVF filter;
		float zr = 1.0f, zi = 0.0f, wr = 1.0f, wi = 0.0f; // the bubble
		float bamp = 0.0f;
	};
	Params target;
	Params cur;
	bool first = true;
	LocalVector<Drop> drops;
	RandomPCG rng = RandomPCG(0x7a1e);
	FellowPink pink_l, pink_r;
	FellowOnePole hp_l, hp_r, lp_l, lp_r, fizz_l, fizz_r, roof_l, roof_r, roof2_l, roof2_r;
	FellowSVF hump_l, hump_r;
	float spawn = 0.0f;
	float roof_spawn = 0.0f;
	Stats stats;
	float _noise() { return rng.randf() * 2.0f - 1.0f; }
	void _drop(bool p_roof, int p_delay);
};

// Wind: a low rumble and a rush whose pitch rises with the speed, both following the gusts; edges whistling (Aeolian
// tones, f = 0.2 v / d, for edges of 3, 7 and 15 mm); leaves rustling in grains.
class FellowWindSynth : public FellowSynthSource {
public:
	struct Params {
		float speed = 0.0f; // m/s at the listener (with the gust there)
		float whistle = 1.0f;
		float leaves = 0.5f;
		float gain = 1.0f;
	};
	void post(const Params &p_params);
	virtual void render(float *r_left, float *r_right, int p_frames) override;
	static FellowSynthSource *make() { return memnew(FellowWindSynth); }

private:
	Params target;
	Params cur;
	bool first = true;
	RandomPCG rng = RandomPCG(0x3170);
	FellowPink pink_l, pink_r;
	FellowOnePole rum_l, rum_r, rum2_l, rum2_r, leaf_l, leaf_r;
	FellowSVF rush_l, rush_r;
	FellowSVF whistle[3];
	float wander[3] = {}; // each edge's slow wander of the wind it feels
	float wander_v[3] = {};
	float flutter = 0.0f; // the rush's slow swell
	float flutter_target = 0.0f;
	float grain_env = 0.0f;
	float grain_pan = 0.5f;
	float _noise() { return rng.randf() * 2.0f - 1.0f; }
};

// Thunder: a strike's channel (its segments) heard from the listener — each segment's pressure pulse (an N-wave,
// longer for the main channel, stretched by the segment's length along the line of sight, weaker so) arriving after its
// distance / 343 s, softened by the air over its distance; the sum rolls for as long as the channel's distances spread.
// Rendered once at the strike (on a worker), played after the first arrival with an outdoor reverberation.
class FellowThunderSynth : public FellowSynthSource {
public:
	struct Segment {
		Vector3 a;
		Vector3 b;
		float weight = 1.0f; // the main channel 1, branches less
	};
	struct Clap {
		PackedFloat32Array dry; // mono
		int delay = 0; // samples before it is heard
		float gl = 0.7f, gr = 0.7f;
	};
	// The dry sound of a strike (from its first arrival) and that arrival's time (s).
	static PackedFloat32Array build(const LocalVector<Segment> &p_segments, const Vector3 &p_listener, const float *p_stroke_time, const float *p_stroke_amplitude, int p_strokes, float p_rate, float &r_first_arrival);
	void post(const Clap &p_clap);
	void set_gain(float p_gain);
	virtual void render(float *r_left, float *r_right, int p_frames) override;
	static FellowSynthSource *make() { return memnew(FellowThunderSynth); }

private:
	struct Playing {
		PackedFloat32Array dry;
		int pos = 0; // negative: samples still to wait
		float gl = 0.7f, gr = 0.7f;
	};
	LocalVector<Clap> pending;
	LocalVector<Playing> playing;
	float gain = 1.0f;
	float pending_gain = 1.0f;
	// The reverberation: four delay lines, mixed (Hadamard), damped.
	LocalVector<float> lines[4];
	int line_pos[4] = {};
	float damp[4] = {};
	bool lines_ready = false;
};
