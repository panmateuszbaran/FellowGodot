/**************************************************************************/
/*  fellow_lightning_3d.cpp                                               */
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

#include "fellow_lightning_3d.h"

#include "fellow_editor_still.h"
#include "fellow_shared_shaders.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "servers/audio/audio_server.h"
#include "servers/rendering/renderer_rd/effects/fellow_weather.h"
#include "servers/rendering/rendering_server.h"

// The bolt: ribbons turned to the camera, a soft core across them (UV.x), the branches dimmer (COLOR.a). Additive,
// unshaded, tested against the scene's depth (buildings hide it), not fogged (fog would add its colour). `flash` is the
// strike's envelope.
static const char *lightning_bolt_shader_code = R"(
shader_type spatial;
render_mode unshaded, blend_add, depth_draw_never, cull_disabled, shadows_disabled, fog_disabled;

uniform vec4 bolt_color : source_color = vec4(0.78, 0.85, 1.0, 1.0);
uniform float brightness = 30.0;
uniform float flash = 0.0;

void fragment() {
	float x = UV.x * 2.0 - 1.0;
	float core = exp(-x * x * 5.0);
	ALBEDO = bolt_color.rgb * (brightness * flash * COLOR.a * core);
}
)";

// The cloud lit from inside above the bolt: a camera-facing disc of the node's scale, brightest at its centre.
static const char *lightning_glow_shader_code = R"(
shader_type spatial;
render_mode unshaded, blend_add, depth_draw_never, cull_disabled, shadows_disabled, fog_disabled, skip_vertex_transform;

uniform vec4 glow_color : source_color = vec4(0.78, 0.85, 1.0, 1.0);
uniform float brightness = 0.6;
uniform float flash = 0.0;

void vertex() {
	vec3 c = MODEL_MATRIX[3].xyz;
	float s = length(MODEL_MATRIX[0].xyz);
	vec3 wp = c + (INV_VIEW_MATRIX[0].xyz * VERTEX.x + INV_VIEW_MATRIX[1].xyz * VERTEX.y) * s;
	VERTEX = (VIEW_MATRIX * vec4(wp, 1.0)).xyz;
}

void fragment() {
	vec2 p = UV * 2.0 - 1.0;
	float r = max(0.0, 1.0 - dot(p, p));
	ALBEDO = glow_color.rgb * (brightness * flash * r * r);
}
)";

void FellowLightning3D::fellow_bake_shader_codes(Vector<String> &r_codes) {
	r_codes.push_back(String(lightning_bolt_shader_code));
	r_codes.push_back(String(lightning_glow_shader_code));
}

uint64_t FellowLightning3D::_hash(uint64_t p_x) {
	// splitmix64
	p_x += 0x9E3779B97F4A7C15ull;
	p_x = (p_x ^ (p_x >> 30)) * 0xBF58476D1CE4E5B9ull;
	p_x = (p_x ^ (p_x >> 27)) * 0x94D049BB133111EBull;
	return p_x ^ (p_x >> 31);
}

void FellowLightning3D::_build_children() {
	if (flash) {
		return;
	}
	flash = memnew(DirectionalLight3D);
	flash->set_name("LightningFlash");
	flash->set_as_top_level(true);
	flash->set_visible(false);
	flash->set_shadow(shadows);
	flash->set_sky_mode(DirectionalLight3D::SKY_MODE_LIGHT_ONLY); // no sun disc in the sky
	flash->set_meta("_fellow_lightning", true); // not a sun (FellowWater3D's glitter, ...)
	add_child(flash, false, INTERNAL_MODE_FRONT);

	bolt_shader = FellowSharedShaders::shader(lightning_bolt_shader_code); // milestone 53: one per session
	bolt_material.instantiate();
	bolt_material->set_shader(bolt_shader);
	bolt_mesh.instantiate();
	{
		// Until the first strike, a degenerate triangle in the bolt's vertex format: a FellowPipelineWarmer3D's shelf
		// draws the hidden bolt (meta _fellow_shelf), so that its pipelines compile with the scene's, not at the first
		// strike (decision 0167). Replaced by the first bolt (_build_bolt()).
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = PackedVector3Array({ Vector3(), Vector3(), Vector3() });
		arrays[Mesh::ARRAY_TEX_UV] = PackedVector2Array({ Vector2(), Vector2(), Vector2() });
		arrays[Mesh::ARRAY_COLOR] = PackedColorArray({ Color(), Color(), Color() });
		bolt_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
		bolt_mesh->surface_set_material(0, bolt_material);
	}
	bolt = memnew(MeshInstance3D);
	bolt->set_name("LightningBolt");
	bolt->set_as_top_level(true);
	bolt->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	bolt->set_mesh(bolt_mesh);
	bolt->set_visible(false);
	bolt->set_meta(SNAME("_fellow_shelf"), true);
	add_child(bolt, false, INTERNAL_MODE_FRONT);

	Ref<Shader> gs = FellowSharedShaders::shader(lightning_glow_shader_code);
	glow_material.instantiate();
	glow_material->set_shader(gs);
	Ref<QuadMesh> q;
	q.instantiate();
	q->set_size(Vector2(2.0f, 2.0f));
	q->set_material(glow_material);
	glow = memnew(MeshInstance3D);
	glow->set_name("LightningGlow");
	glow->set_as_top_level(true);
	glow->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	glow->set_mesh(q);
	glow->set_visible(false);
	glow->set_meta(SNAME("_fellow_shelf"), true);
	add_child(glow, false, INTERNAL_MODE_FRONT);
}

float FellowLightning3D::_envelope(double p_t) const {
	// Each return stroke: a flash rising within a few milliseconds and decaying over about 40 ms.
	float e = 0.0f;
	for (int i = 0; i < current.strokes; i++) {
		double dt = p_t - current.stroke_time[i];
		if (dt >= 0.0) {
			e = MAX(e, current.stroke_amplitude[i] * float(Math::exp(-dt / 0.045)));
		}
	}
	return e;
}

float FellowLightning3D::get_flash() const {
	return current.active ? _envelope(time - current.start) : 0.0f;
}

struct FellowBoltChannel {
	LocalVector<Vector3> points;
	float width = 1.0f;
	float brightness = 1.0f;
};

// The bolt's channels: the main one by midpoint displacement from the cloud to the ground, branches from its upper two
// thirds and a few of their own (the same random draws whether the bolt is drawn or only heard).
static void fellow_bolt_channels(RandomPCG &p_rng, const Vector3 &p_top, const Vector3 &p_bottom, LocalVector<FellowBoltChannel> &r_channels) {
	LocalVector<FellowBoltChannel> &channels = r_channels;
	channels.clear();

	// Midpoint displacement: each segment's middle moved across it by up to a third of its length.
	auto displace = [&](const Vector3 &p_from, const Vector3 &p_to, int p_levels, float p_rough) {
		LocalVector<Vector3> pts;
		pts.push_back(p_from);
		pts.push_back(p_to);
		for (int l = 0; l < p_levels; l++) {
			LocalVector<Vector3> next;
			for (uint32_t i = 0; i + 1 < pts.size(); i++) {
				Vector3 a = pts[i];
				Vector3 b = pts[i + 1];
				float len = a.distance_to(b);
				Vector3 off = Vector3(p_rng.randf() - 0.5f, (p_rng.randf() - 0.5f) * 0.3f, p_rng.randf() - 0.5f) * (len * p_rough);
				next.push_back(a);
				next.push_back((a + b) * 0.5f + off);
			}
			next.push_back(pts[pts.size() - 1]);
			pts = next;
		}
		return pts;
	};

	FellowBoltChannel main_channel;
	main_channel.points = displace(p_top, p_bottom, 7, 0.55f);
	channels.push_back(main_channel);
	// Branches from the upper two thirds of the channel, downwards and outwards, and a few of their own.
	for (int depth = 0; depth < 2; depth++) {
		uint32_t count = channels.size();
		for (uint32_t c = 0; c < count; c++) {
			if ((depth == 0) != (c == 0)) {
				continue; // the main channel's branches first, then the branches' branches
			}
			// A copy: the branches are appended to `channels` below.
			const LocalVector<Vector3> chp(channels[c].points);
			const float ch_width = channels[c].width;
			const float ch_brightness = channels[c].brightness;
			uint32_t n = chp.size();
			for (uint32_t i = n / 10; i < n * 2 / 3; i++) {
				if (p_rng.randf() > (depth == 0 ? 0.06f : 0.03f)) {
					continue;
				}
				Vector3 from = chp[i];
				Vector3 along = (chp[MIN(i + 1, n - 1)] - from).normalized();
				float h = from.y - p_bottom.y;
				float az = p_rng.randf() * Math::TAU;
				Vector3 out = Vector3(Math::cos(az), 0.0f, Math::sin(az));
				Vector3 dir = (along + out * (0.5f + p_rng.randf())).normalized();
				if (dir.y > -0.2f) {
					dir.y = -0.2f;
					dir.normalize();
				}
				float len = h * (0.15f + 0.3f * p_rng.randf());
				FellowBoltChannel br;
				br.points = displace(from, from + dir * len, 5, 0.6f);
				br.width = ch_width * 0.5f;
				br.brightness = ch_brightness * (0.35f + 0.25f * p_rng.randf());
				channels.push_back(br);
			}
		}
	}
}

void FellowLightning3D::_build_bolt(RandomPCG &p_rng, const Vector3 &p_camera) {
	LocalVector<FellowBoltChannel> channels;
	fellow_bolt_channels(p_rng, current.top, current.bottom, channels);
	// Milestone 178: the channel kept for the thunder.
	bolt_segments.clear();
	for (const FellowBoltChannel &ch : channels) {
		for (uint32_t i = 0; i + 1 < ch.points.size(); i++) {
			FellowThunderSynth::Segment seg;
			seg.a = ch.points[i];
			seg.b = ch.points[i + 1];
			seg.weight = ch.width;
			bolt_segments.push_back(seg);
		}
	}

	// Ribbons turned to the camera, of a constant width as seen from it.
	Camera3D *cam = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
	float far_plane = cam ? cam->get_far() : 4000.0f;
	// Beyond the camera's far plane the bolt is drawn nearer, scaled to look the same (still behind the scene).
	float max_d = 1.0f;
	for (const FellowBoltChannel &ch : channels) {
		for (const Vector3 &p : ch.points) {
			max_d = MAX(max_d, p.distance_to(p_camera));
		}
	}
	float k = MIN(1.0f, far_plane * 0.9f / max_d);
	PackedVector3Array verts;
	PackedVector2Array uvs;
	PackedColorArray cols;
	for (const FellowBoltChannel &ch : channels) {
		for (uint32_t i = 0; i + 1 < ch.points.size(); i++) {
			Vector3 a = p_camera + (ch.points[i] - p_camera) * k;
			Vector3 b = p_camera + (ch.points[i + 1] - p_camera) * k;
			Vector3 mid = (a + b) * 0.5f;
			Vector3 side = (b - a).cross(p_camera - mid);
			if (side.length_squared() < 1e-12f) {
				continue;
			}
			side.normalize();
			float wa = bolt_width * ch.width * a.distance_to(p_camera) * 0.5f;
			float wb = bolt_width * ch.width * b.distance_to(p_camera) * 0.5f;
			Vector3 q[4] = { a - side * wa, a + side * wa, b + side * wb, b - side * wb };
			const int idx[6] = { 0, 1, 2, 0, 2, 3 };
			const float ux[4] = { 0.0f, 1.0f, 1.0f, 0.0f };
			for (int t = 0; t < 6; t++) {
				verts.push_back(q[idx[t]]);
				uvs.push_back(Vector2(ux[idx[t]], 0.0f));
				cols.push_back(Color(1.0f, 1.0f, 1.0f, ch.brightness));
			}
		}
	}
	bolt_mesh->clear_surfaces();
	if (verts.is_empty()) {
		return;
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_COLOR] = cols;
	bolt_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	bolt_mesh->surface_set_material(0, bolt_material);
	bolt->set_global_transform(Transform3D());
	// The cloud lit above it.
	Vector3 top = p_camera + (current.top - p_camera) * k;
	glow->set_global_transform(Transform3D(Basis().scaled(Vector3(1, 1, 1) * cloud_height * 0.6f * k), top));
}

void FellowLightning3D::_begin(uint64_t p_seed, float p_distance, float p_azimuth) {
	_build_children();
	RandomPCG rng(p_seed);
	Camera3D *cam = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
	Vector3 c = cam ? cam->get_global_position() : get_global_position();
	float d = p_distance;
	if (d < 0.0f) {
		// Uniform over the ring's area.
		float u = rng.randf();
		d = Math::sqrt(Math::lerp(distance_range.x * distance_range.x, distance_range.y * distance_range.y, real_t(u)));
	}
	float az = p_azimuth > -360.0f ? Math::deg_to_rad(p_azimuth) : rng.randf() * Math::TAU;
	float ground = get_global_position().y;
	current.bottom = Vector3(c.x + Math::cos(az) * d, ground, c.z + Math::sin(az) * d);
	current.top = current.bottom + Vector3((rng.randf() - 0.5f) * 0.5f * cloud_height, cloud_height, (rng.randf() - 0.5f) * 0.5f * cloud_height);
	current.distance = d;
	current.strokes = 2 + int(rng.rand() % 3);
	current.stroke_time[0] = 0.0f;
	current.stroke_amplitude[0] = 1.0f;
	for (int i = 1; i < current.strokes; i++) {
		current.stroke_time[i] = current.stroke_time[i - 1] + 0.04f + 0.08f * rng.randf();
		current.stroke_amplitude[i] = 0.5f + 0.5f * rng.randf();
	}
	current.start = time;
	current.active = true;
	_build_bolt(rng, c);
	// The flash shines from the bolt's middle towards the camera.
	Vector3 from = (current.top + current.bottom) * 0.5f;
	Vector3 dir = (c - from).normalized();
	if (dir.y > -0.1f) {
		dir.y = -0.1f;
		dir.normalize();
	}
	flash->set_global_transform(Transform3D(Basis::looking_at(dir, Vector3(0, 1, 0)), Vector3()));
	_thunder_begin();
	emit_signal(SNAME("struck"), current.bottom, d);
}

// Milestone 178 (decision 0320): the thunder of the strike just begun, built on a worker from its channel as the
// listener is placed now, played after its first arrival.
void FellowLightning3D::_thunder_job(void *p_job) {
	ThunderJob *job = (ThunderJob *)p_job;
	job->dry = FellowThunderSynth::build(job->segments, job->listener, job->stroke_time, job->stroke_amplitude, job->strokes, job->rate, job->first_arrival);
}

void FellowLightning3D::_thunder_begin() {
	if (!sound_enabled || Engine::get_singleton()->is_editor_hint() || !AudioServer::get_singleton() || bolt_segments.is_empty()) {
		return;
	}
	Node3D *listener = fellow_sound_listener(this);
	if (!listener || thunder_jobs.size() >= 4) {
		return;
	}
	ThunderJob *job = memnew(ThunderJob);
	job->segments = bolt_segments;
	const Transform3D lx = listener->get_global_transform();
	job->listener = lx.origin;
	job->strokes = current.strokes;
	for (int i = 0; i < 4; i++) {
		job->stroke_time[i] = current.stroke_time[i];
		job->stroke_amplitude[i] = current.stroke_amplitude[i];
	}
	job->rate = AudioServer::get_singleton()->get_mix_rate();
	job->struck_at = time;
	// From the listener's right (+) or left (-), half the way: thunder is heard from all around too.
	const Vector3 to = (current.bottom - lx.origin).normalized();
	job->pan = CLAMP(to.dot(lx.basis.get_column(0).normalized()), -1.0f, 1.0f) * 0.5f;
	job->task = WorkerThreadPool::get_singleton()->add_native_task(&FellowLightning3D::_thunder_job, job, false, SNAME("FellowLightning3D thunder"));
	thunder_jobs.push_back(job);
}

void FellowLightning3D::_thunder_update() {
	for (uint32_t i = 0; i < thunder_jobs.size();) {
		ThunderJob *job = thunder_jobs[i];
		if (!WorkerThreadPool::get_singleton()->is_task_completed(job->task)) {
			i++;
			continue;
		}
		WorkerThreadPool::get_singleton()->wait_for_task_completion(job->task);
		thunders_built++;
		if (sound_enabled && !job->dry.is_empty()) {
			FellowThunderSynth *synth = (FellowThunderSynth *)thunder_voice.start(this, &FellowThunderSynth::make, sound_bus, sound_volume_db);
			if (synth) {
				FellowThunderSynth::Clap clap;
				clap.dry = job->dry;
				const double waited = time - job->struck_at;
				clap.delay = int(MAX(0.0, double(job->first_arrival) - waited) * job->rate);
				const float ang = (job->pan + 1.0f) * float(Math::PI) * 0.25f;
				clap.gl = Math::cos(ang) * 1.2f;
				clap.gr = Math::sin(ang) * 1.2f;
				synth->post(clap);
				thunder_heard = time;
			}
		}
		memdelete(job);
		thunder_jobs.remove_at(i);
	}
	// Quiet for a minute: the voice stops.
	if (thunder_voice.is_playing() && thunder_jobs.is_empty() && time - thunder_heard > 60.0) {
		thunder_voice.stop();
	}
}

void FellowLightning3D::_thunder_wait() {
	for (ThunderJob *job : thunder_jobs) {
		WorkerThreadPool::get_singleton()->wait_for_task_completion(job->task);
		memdelete(job);
	}
	thunder_jobs.clear();
}

void FellowLightning3D::set_sound_volume_db(float p_db) {
	sound_volume_db = p_db;
	thunder_voice.set_volume_db(p_db);
}

void FellowLightning3D::set_sound_bus(const StringName &p_bus) {
	sound_bus = p_bus;
	thunder_voice.set_bus(p_bus);
}

Dictionary FellowLightning3D::get_sound_stats() const {
	Dictionary d;
	uint64_t frames = 0, usec = 0;
	thunder_voice.get_mix_stats(frames, usec);
	const float rate = AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 48000.0f;
	d["thunders"] = (int64_t)thunders_built;
	d["playing"] = thunder_voice.is_playing();
	d["mixed_seconds"] = double(frames) / rate;
	d["mix_usec_per_second"] = frames > 0 ? double(usec) / (double(frames) / rate) : 0.0;
	return d;
}

// A strike's thunder offline (for tests): a strike at p_distance on p_azimuth_degrees (0: the listener's right) from a
// listener 1.7 m over the node's ground facing -z, its channel drawn as a strike's is (p_seed), heard from the strike's
// start: interleaved stereo.
PackedFloat32Array FellowLightning3D::debug_render_thunder(float p_distance, float p_azimuth_degrees, float p_seconds, float p_rate, int64_t p_seed) const {
	PackedFloat32Array out;
	RandomPCG rng = RandomPCG(uint64_t(p_seed));
	const float az = Math::deg_to_rad(p_azimuth_degrees);
	const Vector3 listener(0.0f, 1.7f, 0.0f);
	const Vector3 bottom(Math::cos(az) * p_distance, 0.0f, Math::sin(az) * p_distance);
	const Vector3 top = bottom + Vector3((rng.randf() - 0.5f) * 0.5f * cloud_height, cloud_height, (rng.randf() - 0.5f) * 0.5f * cloud_height);
	int strokes = 2 + int(rng.rand() % 3);
	float st[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
	float sa[4] = { 1.0f, 0.0f, 0.0f, 0.0f };
	for (int i = 1; i < strokes; i++) {
		st[i] = st[i - 1] + 0.04f + 0.08f * rng.randf();
		sa[i] = 0.5f + 0.5f * rng.randf();
	}
	LocalVector<FellowBoltChannel> channels;
	fellow_bolt_channels(rng, top, bottom, channels);
	LocalVector<FellowThunderSynth::Segment> segments;
	for (const FellowBoltChannel &ch : channels) {
		for (uint32_t i = 0; i + 1 < ch.points.size(); i++) {
			FellowThunderSynth::Segment seg;
			seg.a = ch.points[i];
			seg.b = ch.points[i + 1];
			seg.weight = ch.width;
			segments.push_back(seg);
		}
	}
	float first = 0.0f;
	FellowThunderSynth synth;
	synth.set_rate(p_rate);
	FellowThunderSynth::Clap clap;
	clap.dry = FellowThunderSynth::build(segments, listener, st, sa, strokes, p_rate, first);
	clap.delay = int(first * p_rate);
	const float pan = CLAMP(Math::cos(az), -1.0f, 1.0f) * 0.5f;
	const float ang = (pan + 1.0f) * float(Math::PI) * 0.25f;
	clap.gl = Math::cos(ang) * 1.2f;
	clap.gr = Math::sin(ang) * 1.2f;
	synth.post(clap);
	const int total = int(MAX(0.0f, p_seconds) * p_rate);
	out.resize(total * 2);
	LocalVector<float> l, r;
	l.resize(1024);
	r.resize(1024);
	float *w = out.ptrw();
	for (int pos = 0; pos < total; pos += 1024) {
		const int n = MIN(1024, total - pos);
		synth.render(l.ptr(), r.ptr(), n);
		for (int i = 0; i < n; i++) {
			w[(pos + i) * 2] = l[i];
			w[(pos + i) * 2 + 1] = r[i];
		}
	}
	return out;
}

Vector3 FellowLightning3D::strike(float p_distance, float p_azimuth_degrees) {
	uint64_t s = _hash(uint64_t(seed) ^ _hash(uint64_t(int64_t(Math::floor(time * 1000.0)))));
	_begin(s, p_distance, p_azimuth_degrees);
	return current.bottom;
}

void FellowLightning3D::_rt_set_cloud_flash(PackedByteArray p_state) {
	if (p_state.size() != int64_t(sizeof(RendererRD::FellowWeather::LightningState))) {
		return;
	}
	RendererRD::FellowWeather *w = RendererRD::FellowWeather::get_singleton();
	if (w) {
		RendererRD::FellowWeather::LightningState ls;
		memcpy(&ls, p_state.ptr(), sizeof(ls));
		w->set_lightning_state(ls);
	}
}

void FellowLightning3D::_push_cloud_flash(bool p_on, float p_brightness) {
	if (!p_on && !cloud_state_pushed) {
		return; // nothing to turn off
	}
	RendererRD::FellowWeather::LightningState ls;
	ls.active = p_on && cloud_glow > 0.0f;
	ls.position = current.top;
	// As the colour passes light the scene with a directional light (light_storage.cpp): colour * energy * pi.
	ls.color = color.srgb_to_linear() * (p_brightness * cloud_glow * float(Math::PI));
	ls.radius = cloud_height * 0.6f;
	cloud_state_pushed = ls.active;
	PackedByteArray state;
	state.resize(sizeof(ls));
	memcpy(state.ptrw(), &ls, sizeof(ls));
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowLightning3D::_rt_set_cloud_flash).bind(state));
}

void FellowLightning3D::set_shadows(bool p_enable) {
	shadows = p_enable;
	if (flash) {
		flash->set_shadow(shadows);
	}
}

void FellowLightning3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			_build_children();
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			// Still in the editor with its Animate Nodes toggle off (milestone 52): no time, no strikes (a flash under
			// way ends: it could not fade).
			const bool still = fellow_editor_still();
			if (time_auto && !still) {
				time += get_process_delta_time();
			}
			if (still) {
				current.active = false;
			}
			// Strikes at random: in each second of the clock one with a probability of strikes_per_minute / 60, at a
			// moment within it — a function of the seed and the second, so the same clock gives the same storm.
			if (strikes_per_minute > 0.0f && is_visible_in_tree() && last_time >= 0.0 && time > last_time && time - last_time < 5.0) {
				int64_t s0 = int64_t(Math::floor(last_time));
				int64_t s1 = int64_t(Math::floor(time));
				for (int64_t slot = s0; slot <= s1; slot++) {
					uint64_t h = _hash(uint64_t(seed) * 0x2545F4914F6CDD1Dull + uint64_t(slot));
					float u = float(h & 0xffffffu) / 16777216.0f;
					if (u >= MIN(1.0f, strikes_per_minute / 60.0f)) {
						continue;
					}
					double at = double(slot) + double((h >> 24) & 0xffffu) / 65536.0;
					if (at > last_time && at <= time) {
						double keep = time;
						time = at;
						_begin(_hash(h), -1.0f, -1000.0f);
						time = keep;
					}
				}
			}
			last_time = time;
			if (!thunder_jobs.is_empty() || thunder_voice.is_playing()) {
				_thunder_update();
			}
			float env = 0.0f;
			if (current.active) {
				double t = time - current.start;
				env = _envelope(t);
				if (t > current.stroke_time[MAX(current.strokes - 1, 0)] + 0.4) {
					current.active = false;
					env = 0.0f;
				}
			}
			bool on = env > 0.002f && is_visible_in_tree();
			if (flash) {
				// Brighter near, as a lit cloud is: by the inverse of the distance, 1 km as the reference.
				float closeness = CLAMP(1000.0f / MAX(current.distance, 1.0f), 0.3f, 2.5f);
				const float light_energy = energy * closeness * env;
				// Written when they change (milestone 52): between flashes, nothing (each write had the renderer
				// redraw).
				if (!shown_valid || on != shown_on || light_energy != shown_energy || env != shown_env || closeness != shown_closeness || color != shown_color || bolt_brightness != shown_brightness) {
					flash->set_param(Light3D::PARAM_ENERGY, light_energy);
					flash->set_color(color);
					if (flash->is_visible() != on) {
						flash->set_visible(on);
					}
					bolt->set_visible(on);
					glow->set_visible(on);
					bolt_material->set_shader_parameter("flash", env);
					bolt_material->set_shader_parameter("bolt_color", color);
					bolt_material->set_shader_parameter("brightness", bolt_brightness);
					glow_material->set_shader_parameter("flash", env * closeness);
					glow_material->set_shader_parameter("glow_color", color);
					shown_valid = true;
					shown_on = on;
					shown_energy = light_energy;
					shown_env = env;
					shown_closeness = closeness;
					shown_color = color;
					shown_brightness = bolt_brightness;
				}
				_push_cloud_flash(on, light_energy);
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_thunder_wait();
			thunder_voice.stop();
			last_time = -1.0;
			current.active = false;
			_push_cloud_flash(false, 0.0f);
		} break;
	}
}

void FellowLightning3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("strike", "distance", "azimuth_degrees"), &FellowLightning3D::strike, DEFVAL(-1.0f), DEFVAL(-1000.0f));
	ClassDB::bind_method(D_METHOD("set_sound_enabled", "enabled"), &FellowLightning3D::set_sound_enabled);
	ClassDB::bind_method(D_METHOD("is_sound_enabled"), &FellowLightning3D::is_sound_enabled);
	ClassDB::bind_method(D_METHOD("set_sound_volume_db", "db"), &FellowLightning3D::set_sound_volume_db);
	ClassDB::bind_method(D_METHOD("get_sound_volume_db"), &FellowLightning3D::get_sound_volume_db);
	ClassDB::bind_method(D_METHOD("set_sound_bus", "bus"), &FellowLightning3D::set_sound_bus);
	ClassDB::bind_method(D_METHOD("get_sound_bus"), &FellowLightning3D::get_sound_bus);
	ClassDB::bind_method(D_METHOD("get_sound_stats"), &FellowLightning3D::get_sound_stats);
	ClassDB::bind_method(D_METHOD("debug_render_thunder", "distance", "azimuth_degrees", "seconds", "rate", "seed"), &FellowLightning3D::debug_render_thunder, DEFVAL(48000.0f), DEFVAL(1));
	ClassDB::bind_method(D_METHOD("is_striking"), &FellowLightning3D::is_striking);
	ClassDB::bind_method(D_METHOD("get_flash"), &FellowLightning3D::get_flash);
	ClassDB::bind_method(D_METHOD("get_flash_light"), &FellowLightning3D::get_flash_light);
	ClassDB::bind_method(D_METHOD("set_strikes_per_minute", "rate"), &FellowLightning3D::set_strikes_per_minute);
	ClassDB::bind_method(D_METHOD("get_strikes_per_minute"), &FellowLightning3D::get_strikes_per_minute);
	ClassDB::bind_method(D_METHOD("set_distance_range", "range"), &FellowLightning3D::set_distance_range);
	ClassDB::bind_method(D_METHOD("get_distance_range"), &FellowLightning3D::get_distance_range);
	ClassDB::bind_method(D_METHOD("set_cloud_height", "metres"), &FellowLightning3D::set_cloud_height);
	ClassDB::bind_method(D_METHOD("get_cloud_height"), &FellowLightning3D::get_cloud_height);
	ClassDB::bind_method(D_METHOD("set_energy", "energy"), &FellowLightning3D::set_energy);
	ClassDB::bind_method(D_METHOD("get_energy"), &FellowLightning3D::get_energy);
	ClassDB::bind_method(D_METHOD("set_color", "color"), &FellowLightning3D::set_color);
	ClassDB::bind_method(D_METHOD("get_color"), &FellowLightning3D::get_color);
	ClassDB::bind_method(D_METHOD("set_bolt_brightness", "brightness"), &FellowLightning3D::set_bolt_brightness);
	ClassDB::bind_method(D_METHOD("get_bolt_brightness"), &FellowLightning3D::get_bolt_brightness);
	ClassDB::bind_method(D_METHOD("set_bolt_width", "radians"), &FellowLightning3D::set_bolt_width);
	ClassDB::bind_method(D_METHOD("get_bolt_width"), &FellowLightning3D::get_bolt_width);
	ClassDB::bind_method(D_METHOD("set_cloud_glow", "glow"), &FellowLightning3D::set_cloud_glow);
	ClassDB::bind_method(D_METHOD("get_cloud_glow"), &FellowLightning3D::get_cloud_glow);
	ClassDB::bind_method(D_METHOD("set_shadows", "enable"), &FellowLightning3D::set_shadows);
	ClassDB::bind_method(D_METHOD("get_shadows"), &FellowLightning3D::get_shadows);
	ClassDB::bind_method(D_METHOD("set_seed", "seed"), &FellowLightning3D::set_seed);
	ClassDB::bind_method(D_METHOD("get_seed"), &FellowLightning3D::get_seed);
	ClassDB::bind_method(D_METHOD("set_time", "seconds"), &FellowLightning3D::set_time);
	ClassDB::bind_method(D_METHOD("get_time"), &FellowLightning3D::get_time);
	ClassDB::bind_method(D_METHOD("set_time_auto", "enabled"), &FellowLightning3D::set_time_auto);
	ClassDB::bind_method(D_METHOD("is_time_auto"), &FellowLightning3D::is_time_auto);

	// Milestone 53: grouped in the inspector.
	ADD_GROUP("Strikes", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "strikes_per_minute", PROPERTY_HINT_RANGE, "0,60,0.01"), "set_strikes_per_minute", "get_strikes_per_minute");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "distance_range", PROPERTY_HINT_NONE, "suffix:m"), "set_distance_range", "get_distance_range");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_height", PROPERTY_HINT_RANGE, "10,5000,1,suffix:m"), "set_cloud_height", "get_cloud_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "seed"), "set_seed", "get_seed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time", PROPERTY_HINT_NONE, "suffix:s", PROPERTY_USAGE_NONE), "set_time", "get_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "time_auto"), "set_time_auto", "is_time_auto");
	ADD_GROUP("Look", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "energy", PROPERTY_HINT_RANGE, "0,100,0.01"), "set_energy", "get_energy");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_color", "get_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bolt_brightness", PROPERTY_HINT_RANGE, "0,1000,0.1"), "set_bolt_brightness", "get_bolt_brightness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "bolt_width", PROPERTY_HINT_RANGE, "0.0001,0.05,0.0001,suffix:rad"), "set_bolt_width", "get_bolt_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cloud_glow", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_cloud_glow", "get_cloud_glow");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "shadows"), "set_shadows", "get_shadows");
	ADD_GROUP("Sound", "sound_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sound_enabled"), "set_sound_enabled", "is_sound_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_volume_db", PROPERTY_HINT_RANGE, "-60,24,0.1,suffix:dB"), "set_sound_volume_db", "get_sound_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "sound_bus"), "set_sound_bus", "get_sound_bus");
	ADD_GROUP("", "");

	ADD_SIGNAL(MethodInfo("struck", PropertyInfo(Variant::VECTOR3, "position"), PropertyInfo(Variant::FLOAT, "distance")));
}

FellowLightning3D::FellowLightning3D() {
}
