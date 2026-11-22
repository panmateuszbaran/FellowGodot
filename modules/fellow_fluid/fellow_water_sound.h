/**************************************************************************/
/*  fellow_water_sound.h                                                  */
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
#include "core/os/mutex.h"
#include "core/templates/local_vector.h"
#include "servers/audio/audio_stream.h"

// FellowEngine (milestone 177, decision 0319): water's sound synthesised from the physics of air bubbles (van den Doel,
// "Physically based models for liquid sounds", 2005). What strikes water entrains bubbles; each rings at its Minnaert
// frequency (f = 3.26 m/s / its radius), damped by its viscous, thermal and radiation losses, its pitch rising as it
// nears the surface. A splash is a slap of broadband noise, a cloud of bubbles whose sizes and number grow with what
// struck and how fast, and for a large body the cavity it leaves closing (one big bubble's low "bloop"); water moving
// past a body (a wake, waves lapping) a stream of small bubbles and the rush of moving water. No samples.
class FellowWaterSynth {
public:
	struct Splash {
		float speed = 0.0f; // m/s into the water
		float size = 0.0f; // m across what struck it
		float gain = 1.0f;
		uint32_t seed = 0;
	};

	struct Stats {
		uint64_t splashes = 0;
		uint64_t bubbles = 0; // made
		uint64_t dropped = 0; // over MAX_BUBBLES
		int active = 0;
		int peak_active = 0;
	};

	static constexpr int MAX_BUBBLES = 640;

	void set_rate(float p_rate) { rate = MAX(8000.0f, p_rate); }
	float get_rate() const { return rate; }
	void add_splash(const Splash &p_splash);
	void set_wake(float p_level) { wake_target = MAX(0.0f, p_level); }
	// Mono samples written (not added).
	void render(float *r_out, int p_frames);
	bool is_idle() const;
	void clear();
	const Stats &get_stats() const { return stats; }

	// One bubble (radius in metres) at once (for tests): its frequency, damping and rise as the model has them.
	void add_bubble(float p_radius, float p_amplitude, float p_delay_seconds, float p_rise = 0.1f);
	static float minnaert_frequency(float p_radius) { return 3.26f / p_radius; }
	static float bubble_damping(float p_radius);

private:
	struct Bubble {
		float zr = 1.0f, zi = 0.0f; // the oscillator: a decaying rotor
		float wr = 1.0f, wi = 0.0f; // its step a sample
		float f0 = 1000.0f; // Hz
		float g = 0.999f; // its decay a sample
		float sigma = 0.0f; // the pitch's rise (1/s): f = f0 (1 + sigma t)
		float amp = 0.0f;
		float age = 0.0f; // s
		int delay = 0; // samples before it rings
		bool started = false;
	};
	// A burst of filtered noise (the slap, the thump).
	struct Burst {
		int delay = 0;
		float amp = 0.0f;
		float attack = 0.0f; // the envelope's rise a sample
		float decay = 0.0f; // its fall a sample (multiplier)
		float env = 0.0f;
		bool rising = true;
		float f = 0.1f; // state-variable filter: 2 sin(pi fc / rate)
		float q = 1.0f; // its damping (1 / Q)
		float low = 0.0f, band = 0.0f;
	};

	float rate = 48000.0f;
	LocalVector<Bubble> bubbles;
	LocalVector<Burst> bursts;
	RandomPCG rng = RandomPCG(0x5eed);
	float wake_target = 0.0f;
	float wake = 0.0f;
	float wake_spawn = 0.0f; // bubbles owed to the wake
	float swish_low = 0.0f, swish_band = 0.0f; // its noise's filter
	float lap_low = 0.0f, lap_band = 0.0f;
	Stats stats;

	float _noise() { return rng.randf() * 2.0f - 1.0f; }
	void _spawn(float p_radius, float p_amp, int p_delay, float p_xi, float p_damping_scale = 1.0f);
	void _burst(int p_delay, float p_amp, float p_fc, float p_q, float p_attack_s, float p_decay_s);
};

class FellowWaterSoundPlayback : public AudioStreamPlayback {
	GDCLASS(FellowWaterSoundPlayback, AudioStreamPlayback);

	FellowWaterSynth synth;
	Mutex mutex;
	LocalVector<FellowWaterSynth::Splash> pending;
	LocalVector<FellowWaterSynth::Splash> incoming; // the audio thread's
	FellowWaterSynth::Stats last_stats;
	float pending_wake = 0.0f;
	bool clear_requested = false;
	bool active = false;
	LocalVector<float> mono;
	uint64_t mixed_frames = 0;
	uint64_t mix_usec = 0;

public:
	// Main thread: what the next mix plays.
	void post_splash(const FellowWaterSynth::Splash &p_splash);
	void post_wake(float p_level);
	void post_clear();
	void get_mix_stats(uint64_t &r_frames, uint64_t &r_usec, FellowWaterSynth::Stats &r_stats);

	virtual void start(double p_from_pos = 0.0) override;
	virtual void stop() override;
	virtual bool is_playing() const override { return active; }
	virtual int get_loop_count() const override { return 0; }
	virtual double get_playback_position() const override { return 0.0; }
	virtual void seek(double p_time) override {}
	virtual int mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) override;
};

class FellowWaterSoundStream : public AudioStream {
	GDCLASS(FellowWaterSoundStream, AudioStream);

public:
	virtual Ref<AudioStreamPlayback> instantiate_playback() override;
	virtual String get_stream_name() const override { return "FellowWaterSound"; }
	virtual double get_length() const override { return 0.0; }
	virtual bool is_monophonic() const override { return true; }
};
