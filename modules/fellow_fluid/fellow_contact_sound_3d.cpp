/**************************************************************************/
/*  fellow_contact_sound_3d.cpp                                           */
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

#include "fellow_contact_sound_3d.h"

#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/physics/collision_shape_3d.h"
#include "scene/3d/physics/rigid_body_3d.h"
#include "scene/resources/3d/box_shape_3d.h"
#include "scene/resources/3d/capsule_shape_3d.h"
#include "scene/resources/3d/convex_polygon_shape_3d.h"
#include "scene/resources/3d/cylinder_shape_3d.h"
#include "scene/resources/3d/sphere_shape_3d.h"
#include "scene/resources/mesh.h"
#include "servers/audio/audio_server.h"
#include "servers/physics_3d/physics_server_3d.h"

static const int MODAL_BLOCK = 32;
static const float AIR_DENSITY = 1.2f; // kg/m^3
static const float AIR_SPEED = 343.0f; // m/s
static const float FULL_SCALE_PA = 20.0f; // full scale: 20 Pa (120 dB SPL) at a metre
// The share of a strike's change of velocity that rings in the body's modes (the rest moves it as a whole).
static const float MODAL_SHARE = 0.025f;
static const float SLIDE_GAIN = 0.18f; // at 1 m/s (the drive grows as the speed^0.8: a rough surface's heights)
static const float ROLL_GAIN = 5.6f;

static _FORCE_INLINE_ float modal_soft_limit(float p_x) {
	const float ax = Math::abs(p_x);
	return ax > 0.6f ? SIGN(p_x) * (0.6f + 0.4f * Math::tanh((ax - 0.6f) / 0.4f)) : p_x;
}

// The synthesis.

void FellowModalSynth::set_modes(const LocalVector<Mode> &p_modes, const LocalVector<float> &p_slide_weights) {
	MutexLock lock(mutex);
	posted_modes = p_modes;
	posted_slide_weights = p_slide_weights;
	modes_changed = true;
}

void FellowModalSynth::strike(const Strike &p_strike) {
	MutexLock lock(mutex);
	if (posted_strikes.size() < 64) {
		posted_strikes.push_back(p_strike);
	}
	stats.strikes++;
	stats.quiet = false;
}

void FellowModalSynth::set_contact(const Contact &p_contact) {
	MutexLock lock(mutex);
	posted_contact = p_contact;
	if (p_contact.slide_drive > 0.0f || p_contact.roll_drive > 0.0f) {
		stats.quiet = false;
	}
}

FellowModalSynth::Stats FellowModalSynth::get_stats() {
	MutexLock lock(mutex);
	return stats;
}

// A two-pole resonator ringing at the mode's frequency, its amplitude decaying at its rate (and the contact's): an
// impulse of 1 rings at amplitude 1.
void FellowModalSynth::_tune(Resonator &r, float p_extra) {
	const float d = r.decay + p_extra;
	const float rr = Math::exp(-d / rate);
	const float theta = float(Math::TAU) * r.frequency / rate;
	r.a1 = 2.0f * rr * Math::cos(theta);
	r.a2 = rr * rr;
	r.b = Math::sin(theta);
	r.extra = p_extra;
}

void FellowModalSynth::render(float *r_left, float *r_right, int p_frames) {
	{
		MutexLock lock(mutex);
		if (modes_changed) {
			modes_changed = false;
			const uint32_t keep = MIN(modes.size(), posted_modes.size());
			modes.resize(posted_modes.size());
			for (uint32_t i = 0; i < modes.size(); i++) {
				Resonator &r = modes[i];
				if (i >= keep) {
					r.y1 = 0.0f;
					r.y2 = 0.0f;
				}
				r.frequency = MIN(posted_modes[i].frequency, rate * 0.45f);
				r.decay = posted_modes[i].decay;
				r.gain = posted_modes[i].gain;
				r.slide_weight = i < posted_slide_weights.size() ? posted_slide_weights[i] : 0.5f;
				_tune(r, MAX(0.0f, contact.extra_decay));
			}
		}
		for (const Strike &s : posted_strikes) {
			Pulse p;
			p.strike = s;
			p.length = MAX(1, int(s.duration * rate));
			p.click_length = MAX(1, int(s.click_duration * rate));
			p.pos = -MAX(0, s.delay);
			pulses.push_back(p);
		}
		posted_strikes.clear();
		contact = posted_contact;
	}
	const uint32_t nm = modes.size();
	drive.resize(nm * MODAL_BLOCK);
	rough.resize(MODAL_BLOCK);
	clicks.resize(MODAL_BLOCK);
	float peak = 0.0f;
	for (int b0 = 0; b0 < p_frames; b0 += MODAL_BLOCK) {
		const int n = MIN(MODAL_BLOCK, p_frames - b0);
		float *L = r_left + b0;
		const bool sounding = !pulses.is_empty() || contact.slide_drive > 0.0f || contact.roll_drive > 0.0f;
		if (!sounding && energy < 1e-12f) {
			// Silent: nothing to ring.
			for (int i = 0; i < n; i++) {
				L[i] = 0.0f;
				r_right[b0 + i] = 0.0f;
			}
			continue;
		}
		for (Resonator &r : modes) {
			if (Math::abs(r.extra - contact.extra_decay) > 0.25f) {
				_tune(r, MAX(0.0f, contact.extra_decay));
			}
		}
		memset(drive.ptr(), 0, sizeof(float) * nm * MODAL_BLOCK);
		for (int i = 0; i < n; i++) {
			clicks[i] = 0.0f;
		}
		// The strikes: a half-sine of force over the contact's duration (its integral the change of velocity); the
		// body's own click the force's rate of change.
		for (uint32_t p = 0; p < pulses.size();) {
			Pulse &pu = pulses[p];
			const float k = float(Math::PI) / float(pu.length);
			const float kc = float(Math::PI) / float(pu.click_length);
			const int len = MAX(pu.length, pu.click_length);
			for (int i = 0; i < n; i++, pu.pos++) {
				if (pu.pos < 0 || pu.pos >= len) {
					continue;
				}
				if (pu.pos < pu.length) {
					const float u = pu.strike.dv * 0.5f * k * Math::sin((float(pu.pos) + 0.5f) * k);
					for (uint32_t m = 0; m < nm; m++) {
						drive[m * MODAL_BLOCK + i] += u * pu.strike.weights[m];
					}
				}
				if (pu.pos < pu.click_length) {
					clicks[i] += pu.strike.click * Math::cos((float(pu.pos) + 0.5f) * kc);
				}
			}
			if (pu.pos >= len) {
				pulses.remove_at_unordered(p);
				continue;
			}
			p++;
		}
		// Sliding and rolling: the surfaces' roughness passing under the contact, its features about 0.4 mm (sliding)
		// and 4 mm (rolling: the contact's patch smooths them) apart, so a band rising with the speed.
		const float dt = 1.0f / rate;
		const bool sliding = contact.slide_drive > 0.0f && contact.slide_speed > 0.01f;
		const bool rolling = contact.roll_drive > 0.0f && contact.roll_speed > 0.01f;
		if (sliding) {
			slide_lp1.set(contact.slide_speed / 0.0004f, rate);
			slide_lp2.a = slide_lp1.a;
		}
		if (rolling) {
			roll_lp1.set(contact.roll_speed / 0.004f, rate);
			roll_lp2.a = roll_lp1.a;
		}
		direct_hp.set(800.0f, rate);
		const float slide_amp = sliding ? SLIDE_GAIN * Math::pow(contact.slide_speed, 0.8f) * contact.slide_drive * contact.roughness * dt * 40.0f : 0.0f;
		const float direct_amp = sliding ? contact.direct * Math::pow(contact.slide_speed, 0.8f) * contact.slide_drive * contact.roughness : 0.0f;
		const float roll_amp = rolling ? ROLL_GAIN * contact.roll_drive * contact.roughness * dt * 40.0f : 0.0f;
		float direct[MODAL_BLOCK];
		for (int i = 0; i < n; i++) {
			float x = 0.0f;
			float d = 0.0f;
			if (sliding) {
				const float s = slide_lp2.lp(slide_lp1.lp(pink.next(_noise()) * 6.0f));
				x += slide_amp * s;
				d = direct_amp * direct_hp.hp(s);
			}
			if (rolling) {
				x += roll_amp * roll_lp2.lp(roll_lp1.lp(_noise() * 3.0f));
			}
			rough[i] = x;
			direct[i] = d;
		}
		float e = 0.0f;
		for (int i = 0; i < n; i++) {
			L[i] = clicks[i] + direct[i];
		}
		for (uint32_t m = 0; m < nm; m++) {
			Resonator &r = modes[m];
			const float *in = drive.ptr() + m * MODAL_BLOCK;
			float y1 = r.y1;
			float y2 = r.y2;
			for (int i = 0; i < n; i++) {
				const float y = r.a1 * y1 - r.a2 * y2 + r.b * (in[i] + rough[i] * r.slide_weight);
				y2 = y1;
				y1 = y;
				L[i] += r.gain * y;
			}
			if (Math::abs(y1) + Math::abs(y2) < 1e-15f) {
				y1 = 0.0f; // no denormals (a fast mode long gone while a slow one rings)
				y2 = 0.0f;
			}
			r.y1 = y1;
			r.y2 = y2;
			e += y1 * y1 + y2 * y2;
		}
		energy = e;
		for (int i = 0; i < n; i++) {
			peak = MAX(peak, Math::abs(L[i]));
			L[i] = modal_soft_limit(L[i]);
			r_right[b0 + i] = L[i];
		}
	}
	MutexLock lock(mutex);
	stats.peak = MAX(stats.peak, peak);
	stats.quiet = pulses.is_empty() && energy < 1e-12f && posted_strikes.is_empty() && posted_contact.slide_drive <= 0.0f && posted_contact.roll_drive <= 0.0f;
}

// The materials.

const FellowContactSound3D::MaterialInfo &FellowContactSound3D::material_info(SoundMaterial p_material) {
	// Young's modulus, density, Poisson's ratio, the loss factor (Cremer and Heckl's tables, rounded), roughness.
	static const MaterialInfo infos[MATERIAL_MAX] = {
		{ "wood", 10.0e9f, 600.0f, 0.3f, 0.012f, 1.0f },
		{ "metal", 200.0e9f, 7850.0f, 0.29f, 0.0004f, 0.45f },
		{ "stone", 50.0e9f, 2600.0f, 0.25f, 0.005f, 1.3f },
		{ "glass", 70.0e9f, 2500.0f, 0.22f, 0.0015f, 0.15f },
		{ "plastic", 2.5e9f, 1100.0f, 0.38f, 0.03f, 0.6f },
		{ "rubber", 0.01e9f, 1100.0f, 0.49f, 0.15f, 0.3f },
		{ "ceramic", 100.0e9f, 2400.0f, 0.22f, 0.002f, 0.4f },
	};
	return infos[CLAMP(int(p_material), 0, int(MATERIAL_MAX) - 1)];
}

// The modes of a beam's bending (free at both ends: beta L = 4.730, 7.853, ...; f = beta^2 / (2 pi) c kappa / L^2, kappa
// the section's radius of gyration) for a long body, of plates' bending (f = pi / 2 c h / sqrt(12 (1 - nu^2)) ((m /
// L)^2 + (n / W)^2), h the plate's thickness: the body's for a solid slab, its walls' for a hollow box, each pair of
// faces) otherwise, of a thin ring's bending round a hollow round body (f = c h / (2 pi r^2 sqrt(12)) n (n^2 - 1) /
// sqrt(n^2 + 1)); each a few per cent off (no body is ideal); the lowest p_max between 25 Hz and the rate's 0.45.
void FellowContactSound3D::build_modes(SoundMaterial p_material, const Vector3 &p_size, bool p_round, float p_wall, uint32_t p_seed, int p_max, float p_rate, LocalVector<FellowModalSynth::Mode> &r_modes, LocalVector<ModeShape> &r_shapes) {
	r_modes.clear();
	r_shapes.clear();
	const MaterialInfo &mi = material_info(p_material);
	const float c = Math::sqrt(mi.young / mi.density);
	int ax[3] = { 0, 1, 2 };
	for (int i = 0; i < 3; i++) {
		for (int j = i + 1; j < 3; j++) {
			if (p_size[ax[j]] > p_size[ax[i]]) {
				SWAP(ax[i], ax[j]);
			}
		}
	}
	const float sa = MAX(0.005f, float(p_size[ax[0]]));
	const float sb = MAX(0.003f, float(p_size[ax[1]]));
	const float sc = MAX(0.002f, float(p_size[ax[2]]));
	const bool hollow = p_wall > 0.0f && p_wall < sc * 0.45f;
	const bool elongated = sa >= 2.5f * sb;
	struct Candidate {
		float f;
		ModeShape s;
	};
	LocalVector<Candidate> cand;
	const float top = MIN(20000.0f, p_rate * 0.45f);
	static const float BETA[8] = { 4.7300f, 7.8532f, 10.9956f, 14.1372f, 17.2788f, 20.4204f, 23.5619f, 26.7035f };
	if (elongated) {
		// Bending across each of the two thinner sizes.
		for (int plane = 0; plane < 2; plane++) {
			const float t = plane == 0 ? sb : sc;
			float kappa;
			if (p_round) {
				kappa = hollow ? sb / 2.83f : sb / 4.0f;
			} else {
				kappa = hollow ? t / 2.45f : t / 3.464f;
			}
			const float split = (p_round && plane == 1) ? 1.006f : 1.0f; // a round section's two planes, just apart
			for (int m = 0; m < 8; m++) {
				const float f = BETA[m] * BETA[m] / float(Math::TAU) * c * kappa / (sa * sa) * split;
				ModeShape s;
				s.axis_u = int8_t(ax[0]);
				s.m = uint8_t(m + 2);
				cand.push_back({ f, s });
			}
		}
	}
	const float plate_k = float(Math::PI) * 0.5f * c / Math::sqrt(12.0f * (1.0f - mi.poisson * mi.poisson));
	auto add_plate = [&](int p_u, int p_v, float p_l, float p_w, float p_h) {
		for (int m = 1; m <= 6; m++) {
			for (int n = 1; n <= 6; n++) {
				const float f = plate_k * p_h * ((m / p_l) * (m / p_l) + (n / p_w) * (n / p_w));
				ModeShape s;
				s.axis_u = int8_t(p_u);
				s.axis_v = int8_t(p_v);
				s.m = uint8_t(m);
				s.n = uint8_t(n);
				cand.push_back({ f, s });
			}
		}
	};
	if (p_round && hollow && !elongated) {
		// A can, a barrel: the shell's ring modes round it.
		const float r = sb * 0.5f;
		for (int n = 2; n <= 10; n++) {
			const float f = c * p_wall / (float(Math::TAU) * r * r * 3.464f) * n * (n * n - 1) / Math::sqrt(float(n * n + 1));
			ModeShape s;
			s.axis_u = int8_t(ax[1]);
			s.m = uint8_t(n);
			cand.push_back({ f, s });
		}
		add_plate(ax[1], ax[2], sb, sc, p_wall); // its ends
	} else if (hollow) {
		add_plate(ax[0], ax[1], sa, sb, p_wall);
		add_plate(ax[0], ax[2], sa, sc, p_wall);
		add_plate(ax[1], ax[2], sb, sc, p_wall);
	} else if (!elongated) {
		add_plate(ax[0], ax[1], sa, sb, sc);
	}
	RandomPCG rng = RandomPCG(uint64_t(p_seed) * 0x9e3779b97f4a7c15ull + 0x51a7);
	LocalVector<Candidate> in_band;
	for (const Candidate &cd : cand) {
		const float f = cd.f * (1.0f + 0.06f * (rng.randf() - 0.5f));
		if (f >= 25.0f && f <= top) {
			in_band.push_back({ f, cd.s });
		}
	}
	struct ByFrequency {
		bool operator()(const Candidate &p_a, const Candidate &p_b) const { return p_a.f < p_b.f; }
	};
	in_band.sort_custom<ByFrequency>();
	const int count = MIN(int(in_band.size()), CLAMP(p_max, 1, FellowModalSynth::MAX_MODES));
	for (int i = 0; i < count; i++) {
		FellowModalSynth::Mode m;
		m.frequency = in_band[i].f;
		m.decay = float(Math::PI) * in_band[i].f * mi.loss + 1.5f;
		m.gain = 1.0f;
		r_modes.push_back(m);
		r_shapes.push_back(in_band[i].s);
	}
}

// The node.

int FellowContactSound3D::active_voices = 0;

FellowContactSound3D::FellowContactSound3D() {
}

FellowContactSound3D::~FellowContactSound3D() {
	if (counted) {
		active_voices--;
	}
}

RigidBody3D *FellowContactSound3D::_body() const {
	return Object::cast_to<RigidBody3D>(get_parent());
}

CollisionObject3D *FellowContactSound3D::_collision_object() const {
	return Object::cast_to<CollisionObject3D>(get_parent());
}

void FellowContactSound3D::set_material(SoundMaterial p_material) {
	material = SoundMaterial(CLAMP(int(p_material), 0, int(MATERIAL_MAX) - 1));
	model_dirty = true;
}

void FellowContactSound3D::set_wall_thickness(float p_thickness) {
	wall_thickness = MAX(0.0f, p_thickness);
	model_dirty = true;
}

void FellowContactSound3D::set_mode_count(int p_count) {
	mode_count = CLAMP(p_count, 1, FellowModalSynth::MAX_MODES);
	model_dirty = true;
}

void FellowContactSound3D::set_sound_enabled(bool p_enabled) {
	sound_enabled = p_enabled;
	if (!sound_enabled) {
		_stop_voice();
	}
}

void FellowContactSound3D::set_sound_volume_db(float p_db) {
	sound_volume_db = p_db;
	voice.set_volume_db(p_db);
}

void FellowContactSound3D::set_sound_bus(const StringName &p_bus) {
	sound_bus = p_bus;
	voice.set_bus(p_bus);
}

void FellowContactSound3D::set_sound_unit_size(float p_size) {
	sound_unit_size = MAX(0.1f, p_size);
	voice.set_unit_size(sound_unit_size);
}

void FellowContactSound3D::set_sound_max_distance(float p_distance) {
	sound_max_distance = MAX(1.0f, p_distance);
	voice.set_max_distance(sound_max_distance);
}

// The body's shapes' bounding box (in its space), whether they are all round (cylinders, capsules), their volume; the
// modes, each one's loudness from how well it radiates: the body's largest face S, its pressure at a metre
// rho c v sqrt(sigma S / 2 pi) for v the mode's velocity — sigma the radiation efficiency, (ka)^2 / (1 + (ka)^2) for
// a body of radius a, under a plate's coincidence (c^2 / (1.8 c_L h)) its edges' share (Maidanik).
void FellowContactSound3D::_build_model() {
	model_dirty = false;
	model_posted = false;
	box = AABB();
	round = true;
	volume = 0.0f;
	bool first = true;
	CollisionObject3D *co = _collision_object();
	for (int i = 0; co && i < co->get_child_count(); i++) {
		const CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(co->get_child(i));
		if (!cs || cs->is_disabled() || cs->get_shape().is_null()) {
			continue;
		}
		const Ref<Shape3D> shape = cs->get_shape();
		AABB b;
		float v = 0.0f;
		bool r = false;
		if (Ref<BoxShape3D> bs = shape; bs.is_valid()) {
			b = AABB(-bs->get_size() * 0.5f, bs->get_size());
			v = b.get_volume();
		} else if (Ref<SphereShape3D> sp = shape; sp.is_valid()) {
			const float rad = sp->get_radius();
			b = AABB(Vector3(-rad, -rad, -rad), Vector3(rad, rad, rad) * 2.0f);
			v = 4.0f / 3.0f * float(Math::PI) * rad * rad * rad;
		} else if (Ref<CylinderShape3D> cy = shape; cy.is_valid()) {
			const float rad = cy->get_radius();
			b = AABB(Vector3(-rad, -cy->get_height() * 0.5f, -rad), Vector3(rad * 2.0f, cy->get_height(), rad * 2.0f));
			v = float(Math::PI) * rad * rad * cy->get_height();
			r = true;
		} else if (Ref<CapsuleShape3D> cp = shape; cp.is_valid()) {
			const float rad = cp->get_radius();
			const float h = MAX(cp->get_height(), rad * 2.0f);
			b = AABB(Vector3(-rad, -h * 0.5f, -rad), Vector3(rad * 2.0f, h, rad * 2.0f));
			v = float(Math::PI) * rad * rad * (h - 2.0f * rad) + 4.0f / 3.0f * float(Math::PI) * rad * rad * rad;
			r = true;
		} else if (Ref<ConvexPolygonShape3D> cv = shape; cv.is_valid() && cv->get_points().size() > 0) {
			const Vector<Vector3> pts = cv->get_points();
			b = AABB(pts[0], Vector3());
			for (const Vector3 &p : pts) {
				b.expand_to(p);
			}
			v = b.get_volume() * 0.55f;
		} else {
			const Ref<ArrayMesh> dm = shape->get_debug_mesh();
			b = dm.is_valid() ? dm->get_aabb() : AABB(Vector3(-0.1f, -0.1f, -0.1f), Vector3(0.2f, 0.2f, 0.2f));
			v = b.get_volume() * 0.5f;
		}
		b = cs->get_transform().xform(b);
		box = first ? b : box.merge(b);
		first = false;
		volume += v;
		round = round && r;
	}
	if (first) {
		box = AABB(Vector3(-0.15f, -0.15f, -0.15f), Vector3(0.3f, 0.3f, 0.3f));
		volume = box.get_volume();
		round = false;
	}
	const Vector3 size = box.size;
	const float rate = AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 48000.0f;
	build_modes(material, size, round, wall_thickness, uint32_t(uint64_t(get_instance_id())), mode_count, rate, mode_list, mode_shapes);
	float dims[3] = { float(size.x), float(size.y), float(size.z) };
	for (int i = 0; i < 3; i++) {
		for (int j = i + 1; j < 3; j++) {
			if (dims[j] > dims[i]) {
				SWAP(dims[i], dims[j]);
			}
		}
	}
	area = MAX(1e-4f, dims[0] * dims[1]);
	const float perimeter = 2.0f * (dims[0] + dims[1]);
	const float a = Math::sqrt(area / float(Math::PI));
	const MaterialInfo &mi = material_info(material);
	const float cl = Math::sqrt(mi.young / mi.density);
	const bool hollow = wall_thickness > 0.0f && wall_thickness < dims[2] * 0.45f;
	const float h = hollow ? wall_thickness : dims[2];
	const float coincidence = AIR_SPEED * AIR_SPEED / (1.8f * cl * MAX(h, 1e-4f));
	RandomPCG rng = RandomPCG(uint64_t(get_instance_id()) + 17);
	slide_weights.clear();
	for (uint32_t i = 0; i < mode_list.size(); i++) {
		FellowModalSynth::Mode &m = mode_list[i];
		const float k = float(Math::TAU) * m.frequency / AIR_SPEED;
		float sigma = (k * a) * (k * a) / (1.0f + (k * a) * (k * a));
		if (mode_shapes[i].axis_v >= 0 && m.frequency < coincidence) {
			sigma = MIN(sigma, MIN(1.0f, AIR_SPEED * perimeter / (float(Math::PI) * float(Math::PI) * area * coincidence) * Math::sqrt(m.frequency / coincidence)));
		}
		m.gain = MODAL_SHARE * AIR_DENSITY * AIR_SPEED * Math::sqrt(sigma * area / float(Math::TAU)) / FULL_SCALE_PA;
		slide_weights.push_back(0.3f + 0.7f * rng.randf());
	}
}

// Each mode's shape where the strike lands: cos(m pi u) cos(n pi v) for u, v the place across the body (0 to 1) along
// the mode's axes; never quite a node (no body is ideal).
void FellowContactSound3D::_strike_weights(const Vector3 &p_local, float *r_weights) const {
	for (uint32_t i = 0; i < mode_shapes.size() && i < (uint32_t)FellowModalSynth::MAX_MODES; i++) {
		const ModeShape &s = mode_shapes[i];
		const float u = box.size[s.axis_u] > 0.0f ? CLAMP(float((p_local[s.axis_u] - box.position[s.axis_u]) / box.size[s.axis_u]), 0.0f, 1.0f) : 0.5f;
		float w = Math::cos(float(Math::PI) * s.m * u);
		if (s.axis_v >= 0) {
			const float v = box.size[s.axis_v] > 0.0f ? CLAMP(float((p_local[s.axis_v] - box.position[s.axis_v]) / box.size[s.axis_v]), 0.0f, 1.0f) : 0.5f;
			w *= Math::cos(float(Math::PI) * s.n * v);
		}
		r_weights[i] = 0.85f * w + (w >= 0.0f ? 0.15f : -0.15f);
	}
}

// The Hertzian contact's duration where the blow lands: 2.87 (m^2 / (R E*^2 v))^(1/5) — m the pair's reduced mass, R
// the curvature where they touch (a corner or edge: a sixth of the body's smallest size), E* their combined stiffness.
float FellowContactSound3D::_contact_duration(float p_mass, float p_speed, int p_other) const {
	const MaterialInfo &a = material_info(material);
	const MaterialInfo &b = material_info(SoundMaterial(p_other));
	const float e_star = 1.0f / ((1.0f - a.poisson * a.poisson) / a.young + (1.0f - b.poisson * b.poisson) / b.young);
	const float smallest = MIN(box.size.x, MIN(box.size.y, box.size.z));
	const float r = CLAMP(smallest / 6.0f, 0.002f, 0.05f);
	const float t = 2.87f * Math::pow(p_mass * p_mass / (r * e_star * e_star * MAX(p_speed, 0.01f)), 0.2f);
	return CLAMP(t, 2.0e-5f, 0.03f);
}

// How long the body as a whole takes to slow: the contact's time, or for a hollow body its wall giving way under the
// blow first (a box's: a plate's centre, k = 48 E I / L^3; a round one's: a ring squeezed across, k = 0.56 E L h^3 /
// r^3), half a period pi sqrt(m / k).
float FellowContactSound3D::_body_duration(float p_mass, float p_contact) const {
	const float smallest = MIN(box.size.x, MIN(box.size.y, box.size.z));
	const float largest = MAX(box.size.x, MAX(box.size.y, box.size.z));
	float t = p_contact;
	if (wall_thickness > 0.0f && wall_thickness < smallest * 0.45f) {
		const float e = material_info(material).young;
		const float h3 = wall_thickness * wall_thickness * wall_thickness;
		float k;
		if (round) {
			const float r = MAX(smallest * 0.5f, 0.005f);
			k = 0.56f * e * largest * h3 / (r * r * r);
		} else {
			const float span = MAX(largest, 0.01f);
			k = 48.0f * e * (span * h3 / 12.0f) / (span * span * span);
		}
		t = MAX(t, float(Math::PI) * Math::sqrt(p_mass / MAX(k, 1.0f)));
	}
	return CLAMP(t, 2.0e-5f, 0.03f);
}

// A strike: the modes driven by the contact's force where it lands; the body's own click — its acceleration's change
// radiating as a dipole, rho V (da/dt) / (4 pi c r) at a metre, at most rho c dv a / 2 (the body no longer small
// against the wavelength).
void FellowContactSound3D::_make_strike(float p_dv, float p_mass, float p_speed, int p_other, const Vector3 &p_local, FellowModalSynth::Strike &r_strike) const {
	r_strike.dv = p_dv;
	r_strike.duration = _contact_duration(p_mass, p_speed, p_other);
	r_strike.click_duration = _body_duration(p_mass, r_strike.duration);
	const float tau = r_strike.click_duration;
	const float a_peak = float(Math::PI) * p_dv / (2.0f * tau);
	const float dipole = AIR_DENSITY * volume * float(Math::PI) * a_peak / tau / (4.0f * float(Math::PI) * AIR_SPEED);
	const float radius = Math::pow(MAX(volume, 1e-9f) * 0.2387f, 1.0f / 3.0f);
	const float cap = AIR_DENSITY * AIR_SPEED * p_dv * radius * 0.5f;
	r_strike.click = dipole / (1.0f + dipole / MAX(cap, 1e-6f)) / FULL_SCALE_PA;
	_strike_weights(p_local, r_strike.weights);
}

// Milestone 181 (decision 0323): what a foot on this body stands on — its surface, or its material's (wood a wooden
// floor, metal a deck, stone, glass, plastic and ceramic tiles, rubber a mat).
int FellowContactSound3D::get_ground() const {
	if (surface >= 0) {
		return surface;
	}
	static const int by_material[MATERIAL_MAX] = { 1, 2, 0, 3, 3, 4, 3 }; // FellowFootstepSynth::Ground
	return by_material[CLAMP(int(material), 0, int(MATERIAL_MAX) - 1)];
}

int FellowContactSound3D::_material_of(Object *p_other) {
	Node *node = Object::cast_to<Node>(p_other);
	if (!node) {
		return int(ground_material);
	}
	const ObjectID id = node->get_instance_id();
	if (const int *cached = other_material_cache.getptr(id)) {
		return *cached < 0 ? int(ground_material) : *cached;
	}
	int found = -1;
	for (int i = 0; i < node->get_child_count(); i++) {
		if (const FellowContactSound3D *cs = Object::cast_to<FellowContactSound3D>(node->get_child(i))) {
			found = int(cs->get_material());
			break;
		}
	}
	if (other_material_cache.size() > 256) {
		other_material_cache.clear();
	}
	other_material_cache[id] = found;
	return found < 0 ? int(ground_material) : found;
}

FellowModalSynth *FellowContactSound3D::_voice(bool p_start) {
	if (voice.is_playing()) {
		return (FellowModalSynth *)voice.get_source();
	}
	if (!p_start) {
		return nullptr;
	}
	if (!counted && active_voices >= 24) {
		dropped++;
		return nullptr;
	}
	FellowModalSynth *synth = (FellowModalSynth *)voice.start_3d(this, &FellowModalSynth::make, sound_bus, sound_volume_db, sound_unit_size, sound_max_distance);
	if (!synth) {
		return nullptr;
	}
	if (!counted) {
		counted = true;
		active_voices++;
	}
	synth->set_modes(mode_list, slide_weights);
	model_posted = true;
	quiet_for = 0.0;
	return synth;
}

void FellowContactSound3D::_stop_voice() {
	voice.stop();
	if (counted) {
		counted = false;
		active_voices--;
	}
}

// Every physics tick: the body's contacts (raised to report 8). A contact closing faster than gravity's tick (and a
// third of a metre a second), new or faster than before, is a strike: its change of velocity (the contact's impulse
// over the mass), the contact's duration, where it lands. Contacts held: the slip at the contact (sliding) or the body
// rolling over it; resting damps the modes.
void FellowContactSound3D::_physics_tick(double p_dt) {
	RigidBody3D *body = _body();
	if (!body || !body->is_inside_tree() || !sound_enabled) {
		return;
	}
	tick++;
	if (model_dirty) {
		_build_model();
	}
	PhysicsDirectBodyState3D *state = PhysicsServer3D::get_singleton()->body_get_direct_state(body->get_rid());
	if (!state) {
		return;
	}
	const Transform3D xf = state->get_transform();
	const Node3D *listener = fellow_sound_listener(this);
	const float distance = listener ? float(listener->get_global_position().distance_to(xf.origin)) : 0.0f;
	const bool in_range = distance <= sound_max_distance * 1.1f;
	const Vector3 lin = state->get_linear_velocity();
	const Vector3 ang = state->get_angular_velocity();
	const float inv_mass = state->get_inverse_mass();
	const float mass = inv_mass > 0.0f ? 1.0f / inv_mass : 1.0f;
	const float g = 9.8f;
	const float threshold = MAX(0.3f, 2.5f * g * float(p_dt));
	FellowModalSynth *synth = _voice(false);
	FellowModalSynth::Contact cont;
	float best_slide = 0.0f;
	float slide_drive = 0.0f;
	int touching = 0;
	const MaterialInfo &self_info = material_info(material);
	float roughness = self_info.roughness;
	const int count = state->get_contact_count();
	for (int i = 0; i < count; i++) {
		Object *other = state->get_contact_collider_object(i);
		const uint64_t key = hash_murmur3_one_64(uint64_t(state->get_contact_collider_id(i)), hash_murmur3_one_32(uint32_t(state->get_contact_local_shape(i)) * 131u + uint32_t(state->get_contact_collider_shape(i))));
		PairState &ps = pairs[key];
		const bool was_touching = ps.last_tick + 1 >= tick && ps.last_tick != 0;
		const Vector3 n = state->get_contact_local_normal(i);
		const Vector3 rel = state->get_contact_local_velocity_at_position(i) - state->get_contact_collider_velocity_at_position(i);
		const float vn = -float(rel.dot(n));
		const float vt = float((rel + n * vn).length());
		const int other_material = _material_of(other);
		touching++;
		if (vn > threshold && (!was_touching || vn > 2.0f * ps.last_speed + threshold) && tick - ps.last_impact_tick > 2) {
			ps.last_impact_tick = tick;
			impacts++;
			float m_eff = mass;
			if (const RigidBody3D *ob = Object::cast_to<RigidBody3D>(other)) {
				m_eff = mass * ob->get_mass() / (mass + ob->get_mass());
			}
			const float jn = Math::abs(float(state->get_contact_impulse(i).dot(n)));
			float dv = jn > 0.0f ? jn * inv_mass : 1.3f * vn * m_eff / mass;
			dv = MIN(dv, 2.2f * vn);
			const Vector3 at = state->get_contact_local_position(i);
			emit_signal(SNAME("impact"), at, vn, other);
			if (in_range && (synth || (synth = _voice(true)))) {
				FellowModalSynth::Strike s;
				_make_strike(dv, m_eff, vn, other_material, xf.affine_inverse().xform(at), s);
				synth->strike(s);
			}
		}
		ps.last_tick = tick;
		ps.last_speed = MAX(vn, 0.0f);
		if (vn <= threshold) {
			const MaterialInfo &oi = material_info(SoundMaterial(other_material));
			if (vt > best_slide) {
				best_slide = vt;
				roughness = Math::sqrt(self_info.roughness * oi.roughness);
				slide_drive = 0.5f * g * MAX(float(n.y), 0.3f);
			}
		}
	}
	if (pairs.size() > 64) {
		// Pairs no longer touching for a second.
		LocalVector<uint64_t> old;
		for (const KeyValue<uint64_t, PairState> &kv : pairs) {
			if (kv.value.last_tick + 60 < tick) {
				old.push_back(kv.key);
			}
		}
		for (uint64_t k : old) {
			pairs.erase(k);
		}
	}
	const float speed = float(lin.length());
	const float spin = float(ang.length()) * MAX(float(box.size.length()) * 0.3f, 0.01f);
	if (touching > 0) {
		cont.extra_decay = (speed < 0.05f && spin < 0.05f) ? 25.0f : 12.0f;
		cont.roughness = roughness;
		if (speed > 0.1f && best_slide < 0.3f * speed + 0.05f && spin > 0.5f * speed) {
			cont.roll_speed = speed;
			cont.roll_drive = 0.15f * g;
		} else if (best_slide > 0.08f && speed > 0.05f) { // (not the solver's jitter under a body at rest)
			cont.slide_speed = best_slide;
			cont.slide_drive = slide_drive;
			cont.direct = 0.002f;
		}
	}
	last_slide = cont.slide_speed;
	last_roll = cont.roll_speed;
	slid_seconds += cont.slide_speed > 0.0f ? p_dt : 0.0;
	rolled_seconds += cont.roll_speed > 0.0f ? p_dt : 0.0;
	// A body moving fast near the listener gets its voice ready (a voice's first sound waits for the mix to take it).
	if (!synth && in_range && (speed > 1.0f || cont.slide_drive > 0.0f || cont.roll_drive > 0.0f)) {
		synth = _voice(true);
	}
	if (synth) {
		synth->set_contact(cont);
		voice.place(xf.origin);
		if (!model_posted) {
			synth->set_modes(mode_list, slide_weights);
			model_posted = true;
		}
		const bool quiet = synth->get_stats().quiet;
		quiet_for = (quiet && speed < 0.5f) ? quiet_for + p_dt : 0.0;
		if (quiet_for > 3.0 || !in_range) {
			_stop_voice();
		}
	}
}

void FellowContactSound3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			model_dirty = true;
			if (RigidBody3D *body = _body()) {
				if (body->get_max_contacts_reported() < 8) {
					body->set_max_contacts_reported(8);
				}
			}
			set_physics_process_internal(!Engine::get_singleton()->is_editor_hint() && _body() != nullptr);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			set_physics_process_internal(false);
			_stop_voice();
			pairs.clear();
			other_material_cache.clear();
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			_physics_tick(get_physics_process_delta_time());
		} break;
	}
}

Array FellowContactSound3D::get_modes() {
	if (model_dirty) {
		_build_model();
	}
	Array out;
	for (const FellowModalSynth::Mode &m : mode_list) {
		Dictionary d;
		d["frequency"] = m.frequency;
		d["decay"] = m.decay;
		d["gain"] = m.gain;
		out.push_back(d);
	}
	return out;
}

Dictionary FellowContactSound3D::get_sound_stats() {
	Dictionary d;
	uint64_t frames = 0, usec = 0;
	voice.get_mix_stats(frames, usec);
	const float rate = AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 48000.0f;
	d["playing"] = voice.is_playing();
	d["modes"] = (int64_t)mode_list.size();
	d["impacts"] = (int64_t)impacts;
	d["dropped"] = (int64_t)dropped;
	d["sliding"] = last_slide;
	d["rolling"] = last_roll;
	d["slid_seconds"] = slid_seconds;
	d["rolled_seconds"] = rolled_seconds;
	d["active_voices"] = active_voices;
	if (FellowModalSynth *synth = (FellowModalSynth *)voice.get_source()) {
		const FellowModalSynth::Stats st = synth->get_stats();
		d["strikes"] = (int64_t)st.strikes;
		d["peak"] = st.peak;
	}
	d["mixed_seconds"] = double(frames) / rate;
	d["mix_usec_per_second"] = frames > 0 ? double(usec) / (double(frames) / rate) : 0.0;
	return d;
}

// The body's sound offline (tests): events by time, each a Dictionary — {time, kind: "impact", speed (m/s, closing),
// other (a material), position (in the body's space), mass (kg, else the body's or 1)}, {time, kind: "slide" or
// "roll", speed, other} until {time, kind: "free"} or {time, kind: "rest"}. The samples (interleaved stereo, the same
// in both), the modes, the strikes and the synthesis's time a second of sound.
Dictionary FellowContactSound3D::debug_render(const Array &p_events, float p_seconds, float p_rate, int p_seed) {
	if (model_dirty) {
		_build_model();
	}
	LocalVector<FellowModalSynth::Mode> modes;
	modes = mode_list;
	if (p_rate != (AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 48000.0f)) {
		LocalVector<ModeShape> shapes;
		build_modes(material, box.size, round, wall_thickness, uint32_t(uint64_t(get_instance_id())), mode_count, p_rate, modes, shapes);
		for (uint32_t i = 0; i < modes.size() && i < mode_list.size(); i++) {
			modes[i].gain = mode_list[i].gain;
		}
	}
	FellowModalSynth synth;
	synth.set_rate(p_rate);
	synth.set_seed(uint64_t(p_seed) * 0x9e3779b97f4a7c15ull + 7);
	synth.set_modes(modes, slide_weights);
	const RigidBody3D *body = _body();
	const float body_mass = body ? body->get_mass() : 1.0f;
	PackedFloat32Array out;
	const int total = int(MAX(0.0f, p_seconds) * p_rate);
	out.resize(total * 2);
	float *w = out.ptrw();
	LocalVector<float> l, r;
	int pos = 0;
	uint64_t usec = 0;
	FellowModalSynth::Contact cont;
	PackedFloat32Array durations;
	PackedFloat32Array clicks;
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
		const String kind = ev.get("kind", "impact");
		const float speed = ev.get("speed", 1.0f);
		const int other = CLAMP(int(ev.get("other", int(ground_material))), 0, int(MATERIAL_MAX) - 1);
		const MaterialInfo &oi = material_info(SoundMaterial(other));
		const float g = 9.8f;
		if (kind == "impact") {
			const float mass = ev.get("mass", body_mass);
			const Vector3 at_local = ev.get("position", box.get_center() + Vector3(0.0f, -box.size.y * 0.5f, 0.0f));
			FellowModalSynth::Strike s;
			_make_strike(1.3f * speed, mass, speed, other, at_local, s);
			synth.strike(s);
			durations.push_back(s.duration);
			clicks.push_back(s.click);
			cont.extra_decay = ev.get("held", false) ? 0.0f : 12.0f;
		} else if (kind == "slide") {
			cont = FellowModalSynth::Contact();
			cont.slide_speed = speed;
			cont.slide_drive = 0.5f * g;
			cont.roughness = Math::sqrt(material_info(material).roughness * oi.roughness);
			cont.extra_decay = 12.0f;
			cont.direct = 0.002f;
		} else if (kind == "roll") {
			cont = FellowModalSynth::Contact();
			cont.roll_speed = speed;
			cont.roll_drive = 0.15f * g;
			cont.roughness = Math::sqrt(material_info(material).roughness * oi.roughness);
			cont.extra_decay = 12.0f;
		} else if (kind == "rest") {
			cont = FellowModalSynth::Contact();
			cont.extra_decay = 25.0f;
		} else {
			cont = FellowModalSynth::Contact();
		}
		synth.set_contact(cont);
	}
	Dictionary d;
	d["samples"] = out;
	d["modes"] = get_modes();
	d["strikes"] = (int64_t)synth.get_stats().strikes;
	d["contact_durations"] = durations;
	d["clicks"] = clicks;
	d["usec_per_second"] = total > 0 ? double(usec) / (double(total) / p_rate) : 0.0;
	return d;
}

PackedStringArray FellowContactSound3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	const CollisionObject3D *co = _collision_object();
	if (!co) {
		warnings.push_back(RTR("FellowContactSound3D sounds its parent: make it a child of a RigidBody3D (or of a static body, to give what strikes it its material)."));
	} else {
		bool shapes = false;
		for (int i = 0; i < co->get_child_count(); i++) {
			const CollisionShape3D *cs = Object::cast_to<CollisionShape3D>(co->get_child(i));
			shapes = shapes || (cs && cs->get_shape().is_valid());
		}
		if (!shapes) {
			warnings.push_back(RTR("The body has no CollisionShape3D children: its modes come from a 30 cm box."));
		}
	}
	return warnings;
}

void FellowContactSound3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_material", "material"), &FellowContactSound3D::set_material);
	ClassDB::bind_method(D_METHOD("get_material"), &FellowContactSound3D::get_material);
	ClassDB::bind_method(D_METHOD("set_wall_thickness", "metres"), &FellowContactSound3D::set_wall_thickness);
	ClassDB::bind_method(D_METHOD("get_wall_thickness"), &FellowContactSound3D::get_wall_thickness);
	ClassDB::bind_method(D_METHOD("set_ground_material", "material"), &FellowContactSound3D::set_ground_material);
	ClassDB::bind_method(D_METHOD("get_ground_material"), &FellowContactSound3D::get_ground_material);
	ClassDB::bind_method(D_METHOD("set_surface", "surface"), &FellowContactSound3D::set_surface);
	ClassDB::bind_method(D_METHOD("get_surface"), &FellowContactSound3D::get_surface);
	ClassDB::bind_method(D_METHOD("get_ground"), &FellowContactSound3D::get_ground);
	ClassDB::bind_method(D_METHOD("set_mode_count", "count"), &FellowContactSound3D::set_mode_count);
	ClassDB::bind_method(D_METHOD("get_mode_count"), &FellowContactSound3D::get_mode_count);
	ClassDB::bind_method(D_METHOD("set_sound_enabled", "enabled"), &FellowContactSound3D::set_sound_enabled);
	ClassDB::bind_method(D_METHOD("is_sound_enabled"), &FellowContactSound3D::is_sound_enabled);
	ClassDB::bind_method(D_METHOD("set_sound_volume_db", "db"), &FellowContactSound3D::set_sound_volume_db);
	ClassDB::bind_method(D_METHOD("get_sound_volume_db"), &FellowContactSound3D::get_sound_volume_db);
	ClassDB::bind_method(D_METHOD("set_sound_bus", "bus"), &FellowContactSound3D::set_sound_bus);
	ClassDB::bind_method(D_METHOD("get_sound_bus"), &FellowContactSound3D::get_sound_bus);
	ClassDB::bind_method(D_METHOD("set_sound_unit_size", "metres"), &FellowContactSound3D::set_sound_unit_size);
	ClassDB::bind_method(D_METHOD("get_sound_unit_size"), &FellowContactSound3D::get_sound_unit_size);
	ClassDB::bind_method(D_METHOD("set_sound_max_distance", "metres"), &FellowContactSound3D::set_sound_max_distance);
	ClassDB::bind_method(D_METHOD("get_sound_max_distance"), &FellowContactSound3D::get_sound_max_distance);
	ClassDB::bind_method(D_METHOD("get_modes"), &FellowContactSound3D::get_modes);
	ClassDB::bind_method(D_METHOD("get_sound_stats"), &FellowContactSound3D::get_sound_stats);
	ClassDB::bind_method(D_METHOD("debug_render", "events", "seconds", "rate", "seed"), &FellowContactSound3D::debug_render, DEFVAL(48000.0f), DEFVAL(1));

	ADD_PROPERTY(PropertyInfo(Variant::INT, "material", PROPERTY_HINT_ENUM, "Wood,Metal,Stone,Glass,Plastic,Rubber,Ceramic"), "set_material", "get_material");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wall_thickness", PROPERTY_HINT_RANGE, "0,0.1,0.0005,or_greater,suffix:m"), "set_wall_thickness", "get_wall_thickness");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "ground_material", PROPERTY_HINT_ENUM, "Wood,Metal,Stone,Glass,Plastic,Rubber,Ceramic"), "set_ground_material", "get_ground_material");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "mode_count", PROPERTY_HINT_RANGE, "1,32,1"), "set_mode_count", "get_mode_count");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "surface", PROPERTY_HINT_ENUM, "From Material:-1,Stone:0,Wood:1,Metal:2,Tile:3,Carpet:4,Gravel:5,Sand:6,Snow:7,Grass:8,Leaves:9,Dirt:10,Mud:11,Water:12"), "set_surface", "get_surface");
	ADD_GROUP("Sound", "sound_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sound_enabled"), "set_sound_enabled", "is_sound_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_volume_db", PROPERTY_HINT_RANGE, "-60,24,0.1,suffix:dB"), "set_sound_volume_db", "get_sound_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sound_bus"), "set_sound_bus", "get_sound_bus");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_unit_size", PROPERTY_HINT_RANGE, "0.1,20,0.1,or_greater,suffix:m"), "set_sound_unit_size", "get_sound_unit_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_max_distance", PROPERTY_HINT_RANGE, "1,500,1,or_greater,suffix:m"), "set_sound_max_distance", "get_sound_max_distance");

	ADD_SIGNAL(MethodInfo("impact", PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::FLOAT, "speed"), PropertyInfo(Variant::OBJECT, "collider")));

	BIND_ENUM_CONSTANT(MATERIAL_WOOD);
	BIND_ENUM_CONSTANT(MATERIAL_METAL);
	BIND_ENUM_CONSTANT(MATERIAL_STONE);
	BIND_ENUM_CONSTANT(MATERIAL_GLASS);
	BIND_ENUM_CONSTANT(MATERIAL_PLASTIC);
	BIND_ENUM_CONSTANT(MATERIAL_RUBBER);
	BIND_ENUM_CONSTANT(MATERIAL_CERAMIC);
	BIND_ENUM_CONSTANT(MATERIAL_MAX);
}
