/**************************************************************************/
/*  fellow_water_sound.cpp                                                */
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

#include "fellow_water_sound.h"

#include "core/os/os.h"
#include "servers/audio/audio_server.h"

static const int BLOCK = 32; // samples between a bubble's pitch updates and the wake's spawning
static const int ONSET = 8; // samples of a bubble's onset ramp (a click's broadband edge softened)
static const float MAX_RISE = 1.6f; // a bubble's pitch rises at most this much (it reaches the surface)
static const float LEVEL = 0.6f; // the synthesis' level

// The damping of a bubble's ringing (1/s, of its amplitude): viscous and thermal losses and radiation (van den Doel
// 2005, after Minnaert), 0.043 f + 0.0014 f^1.5 in its frequency.
float FellowWaterSynth::bubble_damping(float p_radius) {
	const float r = MAX(p_radius, 1e-5f);
	return 0.13f / r + 0.0072f / (r * Math::sqrt(r));
}

void FellowWaterSynth::_spawn(float p_radius, float p_amp, int p_delay, float p_xi, float p_damping_scale) {
	if (p_amp <= 0.0f) {
		return;
	}
	Bubble b;
	b.f0 = MIN(minnaert_frequency(p_radius), 0.45f * rate);
	const float d = bubble_damping(p_radius) * p_damping_scale;
	b.g = Math::exp(-d / rate);
	b.sigma = p_xi * d;
	b.amp = p_amp;
	b.delay = MAX(0, p_delay);
	stats.bubbles++;
	if (bubbles.size() >= (uint32_t)MAX_BUBBLES) {
		// Full: the quietest goes if the new one is louder.
		uint32_t q = 0;
		float least = 1e30f;
		for (uint32_t i = 0; i < bubbles.size(); i++) {
			const Bubble &o = bubbles[i];
			const float a = o.amp * o.amp * (o.zr * o.zr + o.zi * o.zi);
			if (a < least) {
				least = a;
				q = i;
			}
		}
		stats.dropped++;
		if (least >= b.amp * b.amp) {
			return;
		}
		bubbles[q] = b;
		return;
	}
	bubbles.push_back(b);
}

void FellowWaterSynth::add_bubble(float p_radius, float p_amplitude, float p_delay_seconds, float p_rise) {
	_spawn(p_radius, p_amplitude, int(p_delay_seconds * rate), p_rise);
}

void FellowWaterSynth::_burst(int p_delay, float p_amp, float p_fc, float p_q, float p_attack_s, float p_decay_s) {
	if (p_amp <= 0.0f || bursts.size() >= 64) {
		return;
	}
	Burst b;
	b.delay = MAX(0, p_delay);
	b.amp = p_amp;
	b.attack = 1.0f / MAX(1.0f, p_attack_s * rate);
	b.decay = Math::exp(-1.0f / MAX(1.0f, p_decay_s * rate));
	b.f = 2.0f * Math::sin(float(Math::PI) * MIN(p_fc, rate / 6.0f) / rate);
	b.q = 1.0f / MAX(0.3f, p_q);
	bursts.push_back(b);
}

// A body striking the water: the slap (broadband, brighter for a small body), the thump of a large one, the bubbles it
// entrains (their number and largest size growing with its size and speed; radii distributed as r^-2.2 from 0.4 mm,
// spread over the entrainment's few hundredths of a second), and behind a large or fast body the cavity closing.
void FellowWaterSynth::add_splash(const Splash &p_splash) {
	const float speed = CLAMP(p_splash.speed, 0.0f, 20.0f);
	const float size = CLAMP(p_splash.size, 0.02f, 4.0f);
	const float v = MIN(speed, 12.0f) / 5.0f; // 1 at 5 m/s
	const float loud = p_splash.gain * CLAMP(0.1f * speed * Math::sqrt(size), 0.0f, 1.5f);
	if (loud <= 1e-4f) {
		return;
	}
	stats.splashes++;
	if (p_splash.seed != 0) {
		rng.seed(p_splash.seed);
	}
	_burst(0, LEVEL * loud * 0.5f * MIN(1.0f, v), CLAMP(1400.0f / Math::sqrt(size), 500.0f, 6000.0f), 1.2f, 0.0008f, 0.006f + 0.025f * size);
	if (size > 0.3f) {
		_burst(0, LEVEL * loud * 0.6f * MIN(1.0f, size), 120.0f + 60.0f / size, 1.0f, 0.002f, 0.03f + 0.05f * size);
	}
	const int count = CLAMP(int(6.0f + 50.0f * size * v), 4, 300);
	const float r_min = 0.0004f;
	const float r_max = CLAMP(0.0015f + 0.005f * size * v, 0.002f, 0.012f);
	const float spread = 0.025f + 0.06f * size; // s
	const float beta = 2.2f;
	const float a = Math::pow(r_min, 1.0f - beta);
	const float b = Math::pow(r_max, 1.0f - beta);
	for (int i = 0; i < count; i++) {
		const float r = Math::pow(a + rng.randf() * (b - a), 1.0f / (1.0f - beta));
		const float t = -spread * Math::log(1.0f - 0.995f * rng.randf());
		const float amp = LEVEL * loud * 0.35f * Math::sqrt(r / 0.002f) * (0.4f + 0.6f * rng.randf());
		_spawn(r, amp, int(t * rate), 0.05f + 0.25f * rng.randf());
	}
	if (size * v > 0.25f) {
		// The cavity: a torn pocket of air, not a sphere — damped 2.5 times a bubble of its size.
		const float rc = CLAMP(0.004f + 0.012f * size * Math::sqrt(v), 0.004f, 0.02f);
		const float delay = CLAMP(0.03f + 0.1f * size / MAX(v, 0.3f), 0.03f, 0.35f);
		_spawn(rc, LEVEL * loud * 0.45f, int(delay * rate), 0.15f, 2.5f);
		for (int k = 0; k < 2; k++) {
			_spawn(rc * (0.45f + 0.25f * rng.randf()), LEVEL * loud * 0.2f, int((delay + 0.01f + 0.04f * rng.randf()) * rate), 0.15f, 2.0f);
		}
	}
}

bool FellowWaterSynth::is_idle() const {
	return bubbles.is_empty() && bursts.is_empty() && wake < 1e-4f && wake_target <= 0.0f;
}

void FellowWaterSynth::clear() {
	bubbles.clear();
	bursts.clear();
	wake = 0.0f;
	wake_target = 0.0f;
	wake_spawn = 0.0f;
	swish_low = swish_band = lap_low = lap_band = 0.0f;
}

void FellowWaterSynth::render(float *r_out, int p_frames) {
	memset(r_out, 0, sizeof(float) * p_frames);
	const float two_pi = float(Math::TAU);
	const float sw_f = 2.0f * Math::sin(float(Math::PI) * 900.0f / rate);
	const float lap_f = 2.0f * Math::sin(float(Math::PI) * 220.0f / rate);
	for (int b0 = 0; b0 < p_frames; b0 += BLOCK) {
		const int n = MIN(BLOCK, p_frames - b0);
		float *out = r_out + b0;
		// The wake: eased to its level (50 ms), its bubbles spawned at their rate, its rush of water.
		wake += (wake_target - wake) * (1.0f - Math::exp(-float(n) / (0.05f * rate)));
		if (wake > 1e-4f) {
			const float w = MIN(wake, 6.0f);
			wake_spawn += 60.0f * wake * float(n) / rate;
			while (wake_spawn >= 1.0f || (wake_spawn > 0.0f && rng.randf() < wake_spawn)) {
				wake_spawn -= 1.0f;
				const float lo = Math::pow(0.0005f, -1.5f);
				const float hi = Math::pow(0.0015f + 0.001f * MIN(wake, 4.0f), -1.5f);
				const float r = Math::pow(lo + rng.randf() * (hi - lo), -1.0f / 1.5f);
				_spawn(r, LEVEL * 0.12f * Math::sqrt(w) * Math::sqrt(r / 0.002f) * (0.3f + 0.7f * rng.randf()), int(rng.randf() * n), 0.3f * rng.randf());
			}
			wake_spawn = MAX(wake_spawn, 0.0f);
			const float sw_amp = LEVEL * 0.02f * Math::pow(w, 0.7f);
			const float lap_amp = LEVEL * 0.03f * Math::sqrt(w);
			for (int i = 0; i < n; i++) {
				const float x = _noise();
				swish_low += sw_f * swish_band;
				swish_band += sw_f * (x - swish_low - 0.7f * swish_band);
				lap_low += lap_f * lap_band;
				lap_band += lap_f * (x - lap_low - 1.0f * lap_band);
				out[i] += sw_amp * swish_band + lap_amp * lap_band;
			}
		}
		// The bubbles.
		for (uint32_t k = 0; k < bubbles.size();) {
			Bubble &bb = bubbles[k];
			int start = 0;
			if (bb.delay >= n) {
				bb.delay -= n;
				k++;
				continue;
			}
			if (bb.delay > 0) {
				start = bb.delay;
				bb.delay = 0;
			}
			const float f = MIN(bb.f0 * MIN(1.0f + bb.sigma * bb.age, MAX_RISE), 0.45f * rate);
			const float th = two_pi * f / rate;
			bb.wr = bb.g * Math::cos(th);
			bb.wi = bb.g * Math::sin(th);
			float zr = bb.zr;
			float zi = bb.zi;
			const float wr = bb.wr;
			const float wi = bb.wi;
			const float amp = bb.amp;
			int i = start;
			if (!bb.started) {
				// Its onset over ONSET samples.
				bb.started = true;
				for (int j = 0; j < ONSET && i < n; j++, i++) {
					const float tr = zr * wr - zi * wi;
					zi = zr * wi + zi * wr;
					zr = tr;
					out[i] += amp * zi * (float(j + 1) / float(ONSET));
				}
			}
			for (; i < n; i++) {
				const float tr = zr * wr - zi * wi;
				zi = zr * wi + zi * wr;
				zr = tr;
				out[i] += amp * zi;
			}
			bb.zr = zr;
			bb.zi = zi;
			bb.age += float(n - start) / rate;
			if (amp * amp * (zr * zr + zi * zi) < 1e-12f) {
				bubbles.remove_at_unordered(k);
				continue;
			}
			k++;
		}
		// The bursts.
		for (uint32_t k = 0; k < bursts.size();) {
			Burst &bu = bursts[k];
			for (int i = 0; i < n; i++) {
				if (bu.delay > 0) {
					bu.delay--;
					continue;
				}
				if (bu.rising) {
					bu.env += bu.attack;
					if (bu.env >= 1.0f) {
						bu.env = 1.0f;
						bu.rising = false;
					}
				} else {
					bu.env *= bu.decay;
				}
				const float x = _noise();
				bu.low += bu.f * bu.band;
				bu.band += bu.f * (x - bu.low - bu.q * bu.band);
				out[i] += bu.amp * bu.env * bu.band;
			}
			if (!bu.rising && bu.env < 1e-4f) {
				bursts.remove_at_unordered(k);
				continue;
			}
			k++;
		}
		// A soft limit: nothing changed under 0.6, nothing over 1.
		for (int i = 0; i < n; i++) {
			const float x = out[i];
			const float ax = Math::abs(x);
			if (ax > 0.6f) {
				out[i] = SIGN(x) * (0.6f + 0.4f * Math::tanh((ax - 0.6f) / 0.4f));
			}
		}
	}
	stats.active = int(bubbles.size());
	stats.peak_active = MAX(stats.peak_active, stats.active);
}

// The playback: the synthesis on the audio thread, fed by the main thread.

void FellowWaterSoundPlayback::post_splash(const FellowWaterSynth::Splash &p_splash) {
	MutexLock lock(mutex);
	if (pending.size() < 64) {
		pending.push_back(p_splash);
	}
}

void FellowWaterSoundPlayback::post_wake(float p_level) {
	MutexLock lock(mutex);
	pending_wake = p_level;
}

void FellowWaterSoundPlayback::post_clear() {
	MutexLock lock(mutex);
	pending.clear();
	pending_wake = 0.0f;
	clear_requested = true;
}

void FellowWaterSoundPlayback::get_mix_stats(uint64_t &r_frames, uint64_t &r_usec, FellowWaterSynth::Stats &r_stats) {
	MutexLock lock(mutex);
	r_frames = mixed_frames;
	r_usec = mix_usec;
	r_stats = last_stats;
}

void FellowWaterSoundPlayback::start(double p_from_pos) {
	synth.set_rate(AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 48000.0f);
	active = true;
}

void FellowWaterSoundPlayback::stop() {
	active = false;
}

int FellowWaterSoundPlayback::mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) {
	if (!active) {
		for (int i = 0; i < p_frames; i++) {
			p_buffer[i] = AudioFrame(0.0f, 0.0f);
		}
		return p_frames;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	// What the main thread posted, taken under the lock; the synthesis outside it.
	bool clear = false;
	float wake_level = 0.0f;
	{
		MutexLock lock(mutex);
		clear = clear_requested;
		clear_requested = false;
		incoming.clear();
		for (const FellowWaterSynth::Splash &s : pending) {
			incoming.push_back(s);
		}
		pending.clear();
		wake_level = pending_wake;
	}
	if (clear) {
		synth.clear();
	}
	for (const FellowWaterSynth::Splash &s : incoming) {
		synth.add_splash(s);
	}
	synth.set_wake(wake_level);
	if (synth.is_idle()) {
		for (int i = 0; i < p_frames; i++) {
			p_buffer[i] = AudioFrame(0.0f, 0.0f);
		}
		return p_frames;
	}
	if ((int)mono.size() < p_frames) {
		mono.resize(p_frames);
	}
	synth.render(mono.ptr(), p_frames);
	for (int i = 0; i < p_frames; i++) {
		p_buffer[i] = AudioFrame(mono[i], mono[i]);
	}
	const uint64_t dt = OS::get_singleton()->get_ticks_usec() - t0;
	MutexLock lock(mutex);
	last_stats = synth.get_stats();
	mixed_frames += p_frames;
	mix_usec += dt;
	return p_frames;
}

Ref<AudioStreamPlayback> FellowWaterSoundStream::instantiate_playback() {
	Ref<FellowWaterSoundPlayback> playback;
	playback.instantiate();
	return playback;
}
