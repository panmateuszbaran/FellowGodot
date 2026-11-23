/**************************************************************************/
/*  fellow_weather_sound.cpp                                              */
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

#include "fellow_weather_sound.h"

#include "fellow_water_sound.h"

#include "core/os/os.h"
#include "scene/3d/audio_listener_3d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/camera_3d.h"
#include "scene/audio/audio_stream_player.h"
#include "scene/main/viewport.h"
#include "servers/audio/audio_server.h"

static const int BLOCK = 32;
static const float RAIN_LEVEL = 2.8f; // heavy rain in the open about -28 dBFS
static const float WIND_LEVEL = 2.0f; // 20 m/s about -19 dBFS

static _FORCE_INLINE_ float soft_limit(float p_x) {
	const float ax = Math::abs(p_x);
	return ax > 0.6f ? SIGN(p_x) * (0.6f + 0.4f * Math::tanh((ax - 0.6f) / 0.4f)) : p_x;
}

void FellowSVF::set(float p_fc, float p_q, float p_rate) {
	f = 2.0f * Math::sin(float(Math::PI) * MIN(p_fc, p_rate / 6.0f) / p_rate);
	q = 1.0f / MAX(0.3f, p_q);
}

void FellowOnePole::set(float p_fc, float p_rate) {
	a = 1.0f - Math::exp(-float(Math::TAU) * MAX(1.0f, p_fc) / p_rate);
}

// The playback.

void FellowSynthPlayback::set_source(FellowSynthSource *p_source) {
	if (source) {
		memdelete(source);
	}
	source = p_source;
}

FellowSynthPlayback::~FellowSynthPlayback() {
	if (source) {
		memdelete(source);
	}
}

void FellowSynthPlayback::get_mix_stats(uint64_t &r_frames, uint64_t &r_usec) const {
	r_frames = mixed_frames.get();
	r_usec = mix_usec.get();
}

void FellowSynthPlayback::start(double p_from_pos) {
	if (source) {
		source->set_rate(AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 48000.0f);
	}
	active = true;
}

int FellowSynthPlayback::mix(AudioFrame *p_buffer, float p_rate_scale, int p_frames) {
	if (!active || !source) {
		for (int i = 0; i < p_frames; i++) {
			p_buffer[i] = AudioFrame(0.0f, 0.0f);
		}
		return p_frames;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	if ((int)left.size() < p_frames) {
		left.resize(p_frames);
		right.resize(p_frames);
	}
	source->render(left.ptr(), right.ptr(), p_frames);
	for (int i = 0; i < p_frames; i++) {
		p_buffer[i] = AudioFrame(left[i], right[i]);
	}
	mixed_frames.add(p_frames);
	mix_usec.add(OS::get_singleton()->get_ticks_usec() - t0);
	return p_frames;
}

Ref<AudioStreamPlayback> FellowSynthStream::instantiate_playback() {
	Ref<FellowSynthPlayback> playback;
	playback.instantiate();
	if (factory) {
		playback->set_source(factory());
	}
	return playback;
}

// The voice.

FellowSynthSource *FellowAmbientVoice::start(Node *p_owner, FellowSynthStream::Factory p_factory, const StringName &p_bus, float p_volume_db) {
	if (!AudioServer::get_singleton() || !p_owner || !p_owner->is_inside_tree()) {
		return nullptr;
	}
	AudioStreamPlayer *p = Object::cast_to<AudioStreamPlayer>(player);
	if (!player) {
		Ref<FellowSynthStream> stream;
		stream.instantiate();
		stream->set_factory(p_factory);
		p = memnew(AudioStreamPlayer);
		p->set_name("Sound");
		p->set_stream(stream);
		p->set_bus(p_bus);
		p->set_volume_db(p_volume_db);
		p_owner->add_child(p, false, Node::INTERNAL_MODE_FRONT);
		player = p;
	}
	if (p && (!p->is_playing() || playback.is_null())) {
		p->play();
		playback = p->get_stream_playback();
	}
	return playback.is_valid() ? playback->get_source() : nullptr;
}

FellowSynthSource *FellowAmbientVoice::start_3d(Node *p_owner, FellowSynthStream::Factory p_factory, const StringName &p_bus, float p_volume_db, float p_unit_size, float p_max_distance) {
	if (!AudioServer::get_singleton() || !p_owner || !p_owner->is_inside_tree()) {
		return nullptr;
	}
	AudioStreamPlayer3D *p = Object::cast_to<AudioStreamPlayer3D>(player);
	if (!player) {
		Ref<FellowSynthStream> stream;
		stream.instantiate();
		stream->set_factory(p_factory);
		p = memnew(AudioStreamPlayer3D);
		p->set_name("Sound");
		p->set_stream(stream);
		p->set_bus(p_bus);
		p->set_volume_db(p_volume_db);
		p->set_unit_size(p_unit_size);
		p->set_max_distance(p_max_distance);
		p->set_as_top_level(true);
		p_owner->add_child(p, false, Node::INTERNAL_MODE_FRONT);
		player = p;
	}
	if (p && (!p->is_playing() || playback.is_null())) {
		p->play();
		playback = p->get_stream_playback();
	}
	return playback.is_valid() ? playback->get_source() : nullptr;
}

void FellowAmbientVoice::place(const Vector3 &p_position) {
	if (AudioStreamPlayer3D *p = Object::cast_to<AudioStreamPlayer3D>(player)) {
		p->set_global_position(p_position);
	}
}

void FellowAmbientVoice::set_unit_size(float p_size) {
	if (AudioStreamPlayer3D *p = Object::cast_to<AudioStreamPlayer3D>(player)) {
		p->set_unit_size(p_size);
	}
}

void FellowAmbientVoice::set_max_distance(float p_distance) {
	if (AudioStreamPlayer3D *p = Object::cast_to<AudioStreamPlayer3D>(player)) {
		p->set_max_distance(p_distance);
	}
}

void FellowAmbientVoice::stop() {
	if (AudioStreamPlayer *p = Object::cast_to<AudioStreamPlayer>(player)) {
		p->stop();
	} else if (AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(player)) {
		p3->stop();
	}
	playback.unref();
}

bool FellowAmbientVoice::is_playing() const {
	bool playing = false;
	if (const AudioStreamPlayer *p = Object::cast_to<AudioStreamPlayer>(player)) {
		playing = p->is_playing();
	} else if (const AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(player)) {
		playing = p3->is_playing();
	}
	return playing && playback.is_valid();
}

void FellowAmbientVoice::set_volume_db(float p_db) {
	if (AudioStreamPlayer *p = Object::cast_to<AudioStreamPlayer>(player)) {
		p->set_volume_db(p_db);
	} else if (AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(player)) {
		p3->set_volume_db(p_db);
	}
}

void FellowAmbientVoice::set_bus(const StringName &p_bus) {
	if (AudioStreamPlayer *p = Object::cast_to<AudioStreamPlayer>(player)) {
		p->set_bus(p_bus);
	} else if (AudioStreamPlayer3D *p3 = Object::cast_to<AudioStreamPlayer3D>(player)) {
		p3->set_bus(p_bus);
	}
}

void FellowAmbientVoice::get_mix_stats(uint64_t &r_frames, uint64_t &r_usec) const {
	r_frames = 0;
	r_usec = 0;
	if (playback.is_valid()) {
		playback->get_mix_stats(r_frames, r_usec);
	}
}

Node3D *fellow_sound_listener(Node *p_node) {
	Viewport *vp = (p_node && p_node->is_inside_tree()) ? p_node->get_viewport() : nullptr;
	if (!vp) {
		return nullptr;
	}
	Node3D *listener = vp->get_audio_listener_3d();
	if (!listener) {
		listener = vp->get_camera_3d();
	}
	return listener;
}

// Fire.

static const float FIRE_LEVEL = 1.0f;

void FellowFireSynth::post(const Params &p_params) {
	MutexLock lock(mutex);
	target = p_params;
}

FellowFireSynth::Stats FellowFireSynth::get_stats() {
	MutexLock lock(mutex);
	return stats;
}

void FellowFireSynth::_crack(int p_delay, float p_amp, float p_fc, float p_q, float p_decay_s, float p_snap_s) {
	if (cracks.size() >= 96) {
		return;
	}
	Crack c;
	c.delay = MAX(0, p_delay);
	c.snap = MAX(1, int(p_snap_s * rate));
	c.amp = p_amp;
	c.decay = Math::exp(-1.0f / MAX(1.0f, p_decay_s * rate));
	c.filter.set(p_fc, p_q, rate);
	cracks.push_back(c);
}

void FellowFireSynth::render(float *r_left, float *r_right, int p_frames) {
	Params t;
	{
		MutexLock lock(mutex);
		t = target;
	}
	if (first) {
		cur = t;
		slow_power = t.power;
		first = false;
	}
	Stats added;
	for (int b0 = 0; b0 < p_frames; b0 += BLOCK) {
		const int n = MIN(BLOCK, p_frames - b0);
		const float dt = float(n) / rate;
		// The flames follow the simulation's fire within 0.12 s (its own flicker comes through), the rest slower.
		const float kp = 1.0f - Math::exp(-dt / 0.12f);
		const float ks = 1.0f - Math::exp(-dt / 0.4f);
		cur.power += (MAX(0.0f, t.power) - cur.power) * kp;
		cur.size += (t.size - cur.size) * ks;
		cur.crackle += (MAX(0.0f, t.crackle) - cur.crackle) * ks;
		cur.embers += (CLAMP(t.embers, 0.0f, 1.0f) - cur.embers) * ks;
		cur.gain = t.gain;
		slow_power += (MAX(0.0f, t.power) - slow_power) * (1.0f - Math::exp(-dt / 0.5f));
		const float power = MIN(cur.power, 200.0f);
		const float size = CLAMP(cur.size, 0.05f, 30.0f);
		const float g = FIRE_LEVEL * cur.gain;
		// A flare: the fire growing much faster than it was (a gas catching, fuel thrown on) — the flame's volume
		// expanding at once, a low "whoomp".
		flare_cooldown -= dt;
		const float rise = MAX(0.0f, t.power) - slow_power;
		if (rise > 0.4f + 0.5f * slow_power && flare_cooldown <= 0.0f) {
			flare_amp = g * 0.12f * Math::sqrt(MIN(rise, 50.0f));
			flare_t = 0.0f;
			flare_cooldown = 1.5f;
			added.flares++;
		}
		// The flames puff at about 1.5 / sqrt(D) Hz, the rate wandering with their flutter.
		const float sq = Math::sqrt(size);
		puff_phase += float(Math::TAU) * (1.5f / sq) * (0.85f + 0.3f * flutter) * dt;
		if (puff_phase > float(Math::TAU)) {
			puff_phase -= float(Math::TAU);
		}
		const float puff = 0.78f + 0.22f * Math::sin(puff_phase);
		if (rng.randf() < 12.0f * dt) {
			flutter_target = rng.randf();
		}
		flutter += (flutter_target - flutter) * (1.0f - Math::exp(-dt / 0.03f));
		const float peak = CLAMP(160.0f / sq, 40.0f, 700.0f);
		// Pink noise between two-pole high- and low-passes at 1 and 1.5 times the peak: it peaks at 0.94 of it, 2.7
		// octaves wide (to -6 dB).
		roar_hp1.set(peak, rate);
		roar_hp2.a = roar_hp1.a;
		roar_lp1.set(peak * 1.5f, rate);
		roar_lp2.a = roar_lp1.a;
		lick.set(600.0f + 500.0f * flutter, 1.2f, rate);
		hiss.set(3000.0f, rate);
		hiss_lp.set(9000.0f, rate);
		flare_lp1.set(90.0f, rate);
		flare_lp2.a = flare_lp1.a;
		const float sp = Math::sqrt(power);
		const float a_roar = g * 0.75f * sp * puff;
		const float a_lick = g * 0.0085f * sp * (0.25f + 0.75f * flutter) * puff;
		const float a_hiss = g * (0.0012f * sp + 0.0015f * cur.embers);
		// Crackles: a snap and a ring; energies Pareto (1.4), the amplitude their root; a run of them one time in seven;
		// a pop of sap now and then.
		spawn += cur.crackle * dt;
		while (spawn >= 1.0f) {
			spawn -= 1.0f;
			const float energy = MIN(100.0f, Math::pow(1.0f - 0.9999f * rng.randf(), -1.0f / 1.4f));
			const float amp = g * 0.034f * Math::sqrt(energy);
			const int at = int(rng.randf() * n);
			if (rng.randf() < 0.07f) {
				_crack(at, amp * 1.6f, 220.0f + 420.0f * rng.randf(), 3.0f, 0.015f + 0.025f * rng.randf(), 0.0008f);
				added.pops++;
			} else {
				_crack(at, amp, 1500.0f + 5500.0f * rng.randf(), 1.5f + 2.5f * rng.randf(), 0.0015f + 0.0045f * rng.randf(), 0.00015f + 0.00035f * rng.randf());
				added.cracks++;
				if (rng.randf() < 0.15f) {
					const int runs = 1 + int(rng.rand() % 4);
					int when = at;
					for (int r = 0; r < runs; r++) {
						when += int((0.008f + 0.042f * rng.randf()) * rate);
						_crack(when, amp * (0.2f + 0.5f * rng.randf()), 1800.0f + 5000.0f * rng.randf(), 2.0f, 0.0015f + 0.003f * rng.randf(), 0.0002f);
						added.cracks++;
					}
				}
			}
		}
		// The embers: a tick now and then as the char cools and splits.
		tick_spawn += cur.embers * 3.0f * dt;
		while (tick_spawn >= 1.0f) {
			tick_spawn -= 1.0f;
			_crack(int(rng.randf() * n), g * 0.006f * (0.3f + 0.7f * rng.randf()), 3000.0f + 5000.0f * rng.randf(), 4.0f, 0.0015f, 0.0001f);
			added.ticks++;
		}
		float *L = r_left + b0;
		float a_flare = 0.0f;
		if (flare_t < 1.5f) {
			a_flare = flare_amp * 6.0f * (1.0f - Math::exp(-flare_t / 0.03f)) * Math::exp(-flare_t / 0.25f);
			flare_t += dt;
		}
		for (int i = 0; i < n; i++) {
			const float w = _noise();
			const float p = pink.next(w);
			const float fl = flare_lp2.lp(flare_lp1.lp(p));
			L[i] = a_roar * roar_lp2.lp(roar_lp1.lp(roar_hp2.hp(roar_hp1.hp(p)))) + a_lick * lick.bp(p) + a_hiss * hiss_lp.lp(hiss.hp(w)) + a_flare * fl;
		}
		for (uint32_t c = 0; c < cracks.size();) {
			Crack &cr = cracks[c];
			if (cr.delay >= n) {
				cr.delay -= n;
				c++;
				continue;
			}
			for (int i = cr.delay; i < n; i++) {
				const float e = _noise();
				float x = cr.filter.bp(e * cr.env) * cr.amp;
				if (cr.snap > 0) {
					x += e * cr.amp * 2.0f;
					cr.snap--;
				}
				cr.env *= cr.decay;
				L[i] += x;
			}
			cr.delay = 0;
			if (cr.env < 1e-4f && cr.snap <= 0) {
				cracks.remove_at_unordered(c);
				continue;
			}
			c++;
		}
		for (int i = 0; i < n; i++) {
			L[i] = soft_limit(L[i]);
			r_right[b0 + i] = L[i];
		}
	}
	MutexLock lock(mutex);
	stats.cracks += added.cracks;
	stats.pops += added.pops;
	stats.ticks += added.ticks;
	stats.flares += added.flares;
}

void fellow_fire_from_flames(float p_volume, float &r_power, float &r_size) {
	const float v = MAX(0.0f, p_volume);
	r_power = v * 1.2e6f / 5.0e4f;
	r_size = CLAMP(Math::pow(v / 0.65f, 1.0f / 3.0f), 0.05f, 30.0f);
}

// Rain.

void FellowRainSynth::post(const Params &p_params) {
	MutexLock lock(mutex);
	target = p_params;
}

FellowRainSynth::Stats FellowRainSynth::get_stats() {
	MutexLock lock(mutex);
	return stats;
}

// A drop near the listener. Its distance uniform over the ground within 0.4-4 m (its level by the inverse); its size
// from Marshall and Palmer's distribution for the rain's rate (exponential, 1 / (4.1 R^-0.21) mm on average). On water
// a tick and, for drops of 0.8-1.1 mm, the small bubble they entrain (Pumphrey and Crum: 13-21 kHz here), for half the
// drops over 2 mm a larger one; on the ground a click through a band of the surface. On the roof over a sheltered
// listener a knock (noise through a resonance of the cover).
void FellowRainSynth::_drop(bool p_roof, int p_delay) {
	if (drops.size() >= 384) {
		return;
	}
	Drop d;
	d.delay = p_delay;
	float amp;
	if (p_roof) {
		amp = 0.14f * (0.35f + 0.65f * rng.randf()) * CLAMP(3.0f / MAX(cur.roof, 0.5f), 0.3f, 2.0f);
		d.filter.set(300.0f + 1100.0f * rng.randf(), 6.0f, rate);
		d.env = 1.0f;
		d.decay = Math::exp(-1.0f / ((0.0008f + 0.0015f * rng.randf()) * rate));
		stats.roof_drops++;
	} else {
		const float r = Math::sqrt(0.16f + rng.randf() * (16.0f - 0.16f));
		amp = 0.12f / r;
		const float rain_rate = 1.0f + 40.0f * cur.rain; // mm/h
		const float lambda = 4.1f * Math::pow(rain_rate, -0.21f); // 1/mm
		const float diameter = MIN(5.0f, 0.5f - Math::log(1.0f - 0.999f * rng.randf()) / lambda);
		if (rng.randf() < cur.water) {
			d.filter.set(3000.0f + 5000.0f * rng.randf(), 0.8f, rate);
			d.env = 0.5f;
			d.decay = Math::exp(-1.0f / (0.0004f * rate));
			float radius = 0.0f;
			float bamp = 0.0f;
			if (diameter >= 0.8f && diameter <= 1.1f) {
				radius = (0.15f + 0.09f * rng.randf()) * 0.001f;
				bamp = amp * 1.4f;
			} else if (diameter > 2.0f && rng.randf() < 0.5f) {
				radius = (0.4f + 1.2f * rng.randf()) * 0.001f;
				bamp = amp * 0.9f;
			}
			if (radius > 0.0f) {
				const float f = MIN(FellowWaterSynth::minnaert_frequency(radius), 0.45f * rate);
				const float g = Math::exp(-FellowWaterSynth::bubble_damping(radius) / rate);
				d.wr = g * Math::cos(float(Math::TAU) * f / rate);
				d.wi = g * Math::sin(float(Math::TAU) * f / rate);
				d.bamp = bamp;
				stats.bubbles++;
			}
		} else {
			d.filter.set(1500.0f + 4500.0f * rng.randf(), 1.2f, rate);
			d.env = 1.0f;
			d.decay = Math::exp(-1.0f / ((0.0003f + 0.0009f * rng.randf()) * rate));
		}
		stats.drops++;
	}
	const float pan = rng.randf() * float(Math::PI) * 0.5f;
	d.gl = Math::cos(pan) * amp;
	d.gr = Math::sin(pan) * amp;
	drops.push_back(d);
}

void FellowRainSynth::render(float *r_left, float *r_right, int p_frames) {
	{
		MutexLock lock(mutex);
		if (first) {
			cur = target;
			first = false;
		}
		const Params t = target;
		const float k = 1.0f - Math::exp(-float(p_frames) / (0.3f * rate));
		cur.rain += (t.rain - cur.rain) * k;
		cur.water += (t.water - cur.water) * k;
		cur.shelter += (t.shelter - cur.shelter) * k;
		cur.roof += (t.roof - cur.roof) * k;
		cur.gain = t.gain;
	}
	const float rain = MAX(0.0f, cur.rain);
	const float open = 1.0f - 0.85f * cur.shelter;
	hp_l.set(300.0f, rate);
	hp_r.a = hp_l.a;
	lp_l.set(Math::lerp(7000.0f, 900.0f, cur.shelter), rate);
	lp_r.a = lp_l.a;
	fizz_l.set(9000.0f, rate);
	fizz_r.a = fizz_l.a;
	roof_l.set(280.0f, rate);
	roof_r.a = roof_l.a;
	roof2_l.a = roof_l.a;
	roof2_r.a = roof_l.a;
	hump_l.set(2500.0f, 0.7f, rate);
	hump_r.f = hump_l.f;
	hump_r.q = hump_l.q;
	const float g = RAIN_LEVEL * cur.gain;
	const float bed = 0.10f * Math::pow(rain, 0.8f) * (1.0f - 0.5f * cur.shelter) * g;
	const float fizz = 0.05f * rain * cur.water * open * g;
	const float rumble = 0.12f * rain * cur.shelter * g;
	for (int b0 = 0; b0 < p_frames; b0 += BLOCK) {
		const int n = MIN(BLOCK, p_frames - b0);
		// The drops due in this block.
		spawn += 320.0f * rain * open * float(n) / rate;
		while (spawn >= 1.0f) {
			spawn -= 1.0f;
			_drop(false, int(rng.randf() * n));
		}
		roof_spawn += 450.0f * rain * cur.shelter * float(n) / rate;
		while (roof_spawn >= 1.0f) {
			roof_spawn -= 1.0f;
			_drop(true, int(rng.randf() * n));
		}
		float *L = r_left + b0;
		float *R = r_right + b0;
		for (int i = 0; i < n; i++) {
			const float wl = _noise();
			const float wr = _noise();
			const float pl = pink_l.next(wl);
			const float pr = pink_r.next(wr);
			// The rain farther off: pink, its lows taken out, a hump about 2.5 kHz, its highs lost on the way (7 kHz);
			// muffled under cover.
			const float ol = lp_l.lp(hp_l.hp(pl) + 0.15f * hump_l.bp(wl));
			const float orr = lp_r.lp(hp_r.hp(pr) + 0.15f * hump_r.bp(wr));
			// On water, the farther drops' small bubbles: a fizz above 9 kHz.
			const float fl = fizz_l.hp(wl);
			const float fr = fizz_r.hp(wr);
			// Under a roof, its drumming's low rumble.
			const float ul = roof2_l.lp(roof_l.lp(pl));
			const float ur = roof2_r.lp(roof_r.lp(pr));
			L[i] = bed * ol + fizz * fl + rumble * ul;
			R[i] = bed * orr + fizz * fr + rumble * ur;
		}
		for (uint32_t k = 0; k < drops.size();) {
			Drop &d = drops[k];
			int i = 0;
			if (d.delay >= n) {
				d.delay -= n;
				k++;
				continue;
			}
			i = d.delay;
			d.delay = 0;
			for (; i < n; i++) {
				float s = 0.0f;
				if (d.env > 1e-4f) {
					s = d.filter.bp(_noise() * d.env);
					d.env *= d.decay;
				} else {
					s = d.filter.bp(0.0f);
				}
				if (d.bamp > 0.0f) {
					const float tr = d.zr * d.wr - d.zi * d.wi;
					d.zi = d.zr * d.wi + d.zi * d.wr;
					d.zr = tr;
					s += d.bamp * d.zi;
				}
				L[i] += d.gl * s * g;
				R[i] += d.gr * s * g;
			}
			const bool knock_done = d.env <= 1e-4f && Math::abs(d.filter.band) < 1e-5f && Math::abs(d.filter.low) < 1e-5f;
			const bool bubble_done = d.bamp <= 0.0f || d.bamp * d.bamp * (d.zr * d.zr + d.zi * d.zi) < 1e-12f;
			if (knock_done && bubble_done) {
				drops.remove_at_unordered(k);
				continue;
			}
			k++;
		}
		for (int i = 0; i < n; i++) {
			L[i] = soft_limit(L[i]);
			R[i] = soft_limit(R[i]);
		}
	}
}

// Wind.

void FellowWindSynth::post(const Params &p_params) {
	MutexLock lock(mutex);
	target = p_params;
}

void FellowWindSynth::render(float *r_left, float *r_right, int p_frames) {
	Params t;
	{
		MutexLock lock(mutex);
		t = target;
	}
	if (first) {
		cur = t;
		first = false;
	}
	static const float EDGES[3] = { 0.003f, 0.007f, 0.015f }; // m: the edges' widths (wires, rails, branches)
	static const float EDGE_PAN[3] = { 0.25f, 0.75f, 0.5f };
	for (int b0 = 0; b0 < p_frames; b0 += BLOCK) {
		const int n = MIN(BLOCK, p_frames - b0);
		// The speed eased over 0.15 s (a gust is heard rising), the rest over 0.3 s.
		cur.speed += (t.speed - cur.speed) * (1.0f - Math::exp(-float(n) / (0.15f * rate)));
		const float k = 1.0f - Math::exp(-float(n) / (0.3f * rate));
		cur.whistle += (t.whistle - cur.whistle) * k;
		cur.leaves += (t.leaves - cur.leaves) * k;
		cur.gain = t.gain;
		const float v = MAX(0.0f, cur.speed);
		const float v10 = v / 10.0f;
		// The rush swells and eases (turbulence a few times a second).
		if (rng.randf() < float(n) / (0.35f * rate)) {
			flutter_target = rng.randf();
		}
		flutter += (flutter_target - flutter) * (1.0f - Math::exp(-float(n) / (0.2f * rate)));
		rum_l.set(70.0f + 6.0f * v, rate);
		rum_r.a = rum_l.a;
		rum2_l.a = rum_l.a;
		rum2_r.a = rum_l.a;
		rush_l.set(220.0f + 45.0f * v, 0.7f, rate);
		rush_r.f = rush_l.f;
		rush_r.q = rush_l.q;
		leaf_l.set(2500.0f, rate);
		leaf_r.a = leaf_l.a;
		const float a_rum = WIND_LEVEL * 0.15f * Math::pow(v10, 1.5f) * cur.gain;
		const float a_rush = WIND_LEVEL * 0.12f * v10 * v10 * (0.55f + 0.45f * flutter) * cur.gain;
		float a_wh[3];
		const float wh_on = cur.whistle * CLAMP((v - 5.0f) / 9.0f, 0.0f, 1.0f);
		for (int e = 0; e < 3; e++) {
			// Each edge feels the wind a little differently, wandering.
			wander_v[e] += (rng.randf() - 0.5f) * 0.02f - wander_v[e] * 0.01f;
			wander[e] = CLAMP(wander[e] + wander_v[e] * 0.05f - wander[e] * 0.002f, -1.0f, 1.0f);
			const float f = 0.2f * v * (1.0f + 0.15f * wander[e]) / EDGES[e];
			whistle[e].set(MAX(f, 20.0f), 40.0f, rate);
			a_wh[e] = WIND_LEVEL * 0.012f * wh_on * (0.3f + 0.7f * (0.5f + 0.5f * wander[e])) * cur.gain;
		}
		// Leaves: grains of rustle, more the stronger the wind.
		if (rng.randf() < 40.0f * cur.leaves * MIN(v / 6.0f, 2.0f) * float(n) / rate) {
			grain_env = 0.5f + 0.5f * rng.randf();
			grain_pan = rng.randf();
		}
		const float grain_decay = Math::exp(-float(n) / (0.03f * rate));
		const float a_leaf = WIND_LEVEL * 0.05f * cur.leaves * MIN(v / 8.0f, 1.5f) * cur.gain;
		float *L = r_left + b0;
		float *R = r_right + b0;
		for (int i = 0; i < n; i++) {
			const float wl = _noise();
			const float wr = _noise();
			const float pl = pink_l.next(wl);
			const float pr = pink_r.next(wr);
			float l = a_rum * rum2_l.lp(rum_l.lp(pl)) + a_rush * rush_l.bp(pl);
			float r = a_rum * rum2_r.lp(rum_r.lp(pr)) + a_rush * rush_r.bp(pr);
			const float w0 = 0.5f * (wl + wr);
			for (int e = 0; e < 3; e++) {
				const float s = a_wh[e] * whistle[e].bp(w0);
				l += s * (1.0f - EDGE_PAN[e]) * 1.4f;
				r += s * EDGE_PAN[e] * 1.4f;
			}
			const float g = a_leaf * grain_env;
			l += g * (1.0f - grain_pan) * leaf_l.hp(wl);
			r += g * grain_pan * leaf_r.hp(wr);
			L[i] = soft_limit(l);
			R[i] = soft_limit(r);
		}
		grain_env *= grain_decay;
	}
}

// Thunder.

PackedFloat32Array FellowThunderSynth::build(const LocalVector<Segment> &p_segments, const Vector3 &p_listener, const float *p_stroke_time, const float *p_stroke_amplitude, int p_strokes, float p_rate, float &r_first_arrival) {
	PackedFloat32Array out;
	r_first_arrival = 0.0f;
	if (p_segments.is_empty() || p_strokes <= 0) {
		return out;
	}
	const float c = 343.0f;
	RandomPCG rng(uint64_t(p_segments.size()) * 0x9E3779B97F4A7C15ull + uint64_t(Math::abs(p_listener.x) * 10.0f + Math::abs(p_listener.z)));
	// The channel in the cloud (most flashes to the ground run kilometres through the cloud, unseen): from the top of
	// the drawn channel, 3-6 km of a wandering horizontal walk in 25 m steps — its far parts the long roll.
	LocalVector<Segment> segments;
	segments.reserve(p_segments.size() + 256);
	for (const Segment &s : p_segments) {
		segments.push_back(s);
	}
	{
		Vector3 top = p_segments[0].a;
		for (const Segment &s : p_segments) {
			if (s.a.y > top.y) {
				top = s.a;
			}
		}
		float heading = rng.randf() * float(Math::TAU);
		const int steps = int((3000.0f + 3000.0f * rng.randf()) / 25.0f);
		Vector3 p = top;
		for (int i = 0; i < steps; i++) {
			heading += (rng.randf() - 0.5f) * 0.6f;
			Segment s;
			s.a = p;
			p += Vector3(Math::cos(heading) * 25.0f, (rng.randf() - 0.5f) * 8.0f, Math::sin(heading) * 25.0f);
			s.b = p;
			s.weight = 0.5f;
			segments.push_back(s);
		}
	}
	float d_min = 1e30f;
	float d_max = 0.0f;
	for (const Segment &s : segments) {
		const float d = ((s.a + s.b) * 0.5f).distance_to(p_listener);
		d_min = MIN(d_min, d);
		d_max = MAX(d_max, d);
	}
	r_first_arrival = d_min / c + p_stroke_time[0];
	const float seconds = MIN(15.0f, (d_max - d_min) / c + p_stroke_time[p_strokes - 1] + 0.6f);
	const int total = int(seconds * p_rate);
	out.resize(total);
	float *w = out.ptrw();
	memset(w, 0, sizeof(float) * total);
	// One N-wave of the segment's (n samples of amplitude a, softened by the air's one pole k), added from `start`.
	auto add_wave = [&](int p_start, int p_n, float p_amp, float p_k, int p_tail) {
		float y = 0.0f;
		for (int i = 0; i < p_n + p_tail; i++) {
			const int at = p_start + i;
			if (at < 0) {
				continue;
			}
			if (at >= total) {
				break;
			}
			const float x = i < p_n ? p_amp * (1.0f - 2.0f * float(i) / float(p_n)) : 0.0f;
			y += p_k * (x - y);
			w[at] += y;
		}
	};
	for (int s = 0; s < p_strokes; s++) {
		for (const Segment &seg : segments) {
			const Vector3 mid = (seg.a + seg.b) * 0.5f;
			const Vector3 along = seg.b - seg.a;
			const float len = along.length();
			if (len < 1e-3f) {
				continue;
			}
			const Vector3 to = mid - p_listener;
			const float d = MAX(to.length(), 30.0f);
			const float cos_t = Math::abs(along.dot(to)) / (len * d);
			const float t = p_stroke_time[s] + to.length() / c - r_first_arrival;
			// The N-wave: 6-12 ms for the main channel, 2.5-5.5 ms for a branch, lengthening with distance; stretched by
			// the segment's extent along the line of sight (broadside it arrives at once: a clap).
			const float main = seg.weight >= 0.9f ? 1.0f : 0.0f;
			const float T = (main > 0.0f ? 0.006f + 0.006f * rng.randf() : 0.0025f + 0.003f * rng.randf()) * (1.0f + d / 3000.0f);
			const float stretch = len * cos_t / c;
			const float Tp = T + stretch;
			const float amp = p_stroke_amplitude[s] * seg.weight * Math::sqrt(len / 10.0f) * (1000.0f / d) * (T / Tp);
			// The air: the highs lost over the distance.
			const float fc = 14000.0f / (1.0f + d / 250.0f);
			const float a = 1.0f - Math::exp(-float(Math::TAU) * fc / p_rate);
			const int start = int(t * p_rate);
			const int tail = int(4.0f / (float(Math::TAU) * fc) * p_rate) + 2;
			add_wave(start, MAX(2, int(Tp * p_rate)), amp, a, tail);
			// The channel's small kinks (a metre and less): short pulses spread over the segment's arrival — the
			// crackle of a near strike, lost to the air from a far one.
			for (int m = 0; m < 5; m++) {
				const float tm = t + rng.randf() * (stretch + 0.002f);
				const float Tm = 0.0006f + 0.0014f * rng.randf();
				add_wave(int(tm * p_rate), MAX(2, int(Tm * p_rate)), amp * (Tp / T) * 0.3f * (0.5f + rng.randf()), a, tail);
			}
		}
	}
	// The level (a strike 300 m away about -11 dBFS at its loudest, before the reverberation).
	for (int i = 0; i < total; i++) {
		w[i] *= 0.07f;
	}
	return out;
}

void FellowThunderSynth::post(const Clap &p_clap) {
	MutexLock lock(mutex);
	if (pending.size() < 8) {
		pending.push_back(p_clap);
	}
}

void FellowThunderSynth::set_gain(float p_gain) {
	MutexLock lock(mutex);
	pending_gain = p_gain;
}

void FellowThunderSynth::render(float *r_left, float *r_right, int p_frames) {
	{
		MutexLock lock(mutex);
		for (const Clap &c : pending) {
			Playing p;
			p.dry = c.dry;
			p.pos = -c.delay;
			p.gl = c.gl;
			p.gr = c.gr;
			playing.push_back(p);
		}
		pending.clear();
		gain = pending_gain;
	}
	if (!lines_ready) {
		static const float DELAYS[4] = { 0.0797f, 0.1173f, 0.1561f, 0.2039f }; // s: echoes off the land and clouds
		for (int k = 0; k < 4; k++) {
			lines[k].resize(MAX(1, int(DELAYS[k] * rate)));
			memset(lines[k].ptr(), 0, sizeof(float) * lines[k].size());
			line_pos[k] = 0;
			damp[k] = 0.0f;
		}
		lines_ready = true;
	}
	const float a_damp = 1.0f - Math::exp(-float(Math::TAU) * 1200.0f / rate);
	for (int i = 0; i < p_frames; i++) {
		float l = 0.0f;
		float r = 0.0f;
		float mono = 0.0f;
		for (Playing &p : playing) {
			if (p.pos < 0) {
				p.pos++;
				continue;
			}
			if (p.pos < p.dry.size()) {
				const float x = p.dry[p.pos] * gain;
				l += x * p.gl;
				r += x * p.gr;
				mono += x;
			}
			p.pos++;
		}
		// The reverberation (its tail rolling after the claps).
		float o[4];
		for (int k = 0; k < 4; k++) {
			o[k] = lines[k][line_pos[k]];
		}
		const float h[4] = { 0.5f * (o[0] + o[1] + o[2] + o[3]), 0.5f * (o[0] - o[1] + o[2] - o[3]), 0.5f * (o[0] + o[1] - o[2] - o[3]), 0.5f * (o[0] - o[1] - o[2] + o[3]) };
		for (int k = 0; k < 4; k++) {
			damp[k] += a_damp * (0.72f * h[k] + 0.25f * mono - damp[k]);
			lines[k][line_pos[k]] = damp[k];
			line_pos[k] = line_pos[k] + 1 >= (int)lines[k].size() ? 0 : line_pos[k] + 1;
		}
		l += 0.55f * (o[0] + o[2]);
		r += 0.55f * (o[1] + o[3]);
		r_left[i] = soft_limit(l);
		r_right[i] = soft_limit(r);
	}
	for (uint32_t k = 0; k < playing.size();) {
		if (playing[k].pos >= playing[k].dry.size()) {
			playing.remove_at_unordered(k);
			continue;
		}
		k++;
	}
}
