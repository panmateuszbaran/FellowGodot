/**************************************************************************/
/*  fellow_vehicle_effects_3d.cpp                                         */
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

#include "fellow_vehicle_effects_3d.h"

#include "fellow_vehicle_3d.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/audio_listener_3d.h"
#include "scene/3d/audio_stream_player_3d.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/curve_texture.h"
#include "scene/resources/gradient_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"
#include "servers/audio/audio_server.h"
#include "servers/audio/effects/audio_stream_generator.h"
#include "servers/rendering/rendering_server.h"

FellowVehicleEffects3D::GlobalStats FellowVehicleEffects3D::global_stats;

void FellowVehicleEffects3D::set_max_mark_segments(int p_count) {
	max_mark_segments = CLAMP(p_count, 8, 65536 / 4);
}

void FellowVehicleEffects3D::set_sound(bool p_enabled) {
	if (sound == p_enabled) {
		return;
	}
	sound = p_enabled;
	if (audio) {
		if (sound) {
			audio->play();
			playback = audio->get_stream_playback();
		} else {
			audio->stop();
			playback.unref();
		}
	}
}

void FellowVehicleEffects3D::_setup() {
	vehicle = Object::cast_to<FellowVehicle3D>(get_parent());
	if (!vehicle) {
		return;
	}
	wheels.clear();
	for (int i = 0; i < vehicle->get_child_count(); i++) {
		if (FellowWheel3D *w = Object::cast_to<FellowWheel3D>(vehicle->get_child(i))) {
			WheelFx fx;
			fx.wheel = w->get_instance_id();
			wheels.push_back(fx);
		}
	}

	// The marks' mesh: max_mark_segments quads (empty, transparent), updated a quad at a time.
	if (skid_marks) {
		const int nv = max_mark_segments * 4;
		PackedVector3Array verts;
		verts.resize(nv);
		PackedColorArray colors;
		colors.resize(nv);
		colors.fill(Color(1, 1, 1, 0));
		PackedInt32Array indices;
		indices.resize(max_mark_segments * 6);
		int32_t *iw = indices.ptrw();
		for (int q = 0; q < max_mark_segments; q++) {
			const int b = q * 4;
			iw[q * 6 + 0] = b;
			iw[q * 6 + 1] = b + 1;
			iw[q * 6 + 2] = b + 2;
			iw[q * 6 + 3] = b + 2;
			iw[q * 6 + 4] = b + 1;
			iw[q * 6 + 5] = b + 3;
		}
		Array arrays;
		arrays.resize(Mesh::ARRAY_MAX);
		arrays[Mesh::ARRAY_VERTEX] = verts;
		arrays[Mesh::ARRAY_COLOR] = colors;
		arrays[Mesh::ARRAY_INDEX] = indices;
		marks_mesh.instantiate();
		marks_mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays, Array(), Dictionary(), Mesh::ARRAY_FLAG_USE_DYNAMIC_UPDATE);
		const BitField<Mesh::ArrayFormat> format = marks_mesh->surface_get_format(0);
		RenderingServer *rs = RenderingServer::get_singleton();
		vertex_stride = rs->mesh_surface_get_format_vertex_stride(BitField<RSE::ArrayFormat>(uint64_t(format)), nv);
		attribute_stride = rs->mesh_surface_get_format_attribute_stride(BitField<RSE::ArrayFormat>(uint64_t(format)), nv);
		color_offset = rs->mesh_surface_get_format_offset(BitField<RSE::ArrayFormat>(uint64_t(format)), nv, RSE::ARRAY_COLOR);
		if (vertex_stride != 12 || attribute_stride < 4) {
			ERR_PRINT(vformat("FellowVehicleEffects3D: unexpected vertex layout (%d, %d): no skid marks.", vertex_stride, attribute_stride));
			marks_mesh.unref();
		} else {
			marks_material.instantiate();
			marks_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
			marks_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
			marks_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
			marks_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
			marks_material->set_albedo(Color(0.02f, 0.02f, 0.02f, 1.0f));
			marks_material->set_render_priority(-1);
			marks_mesh->surface_set_material(0, marks_material);
			marks = memnew(MeshInstance3D);
			marks->set_name("SkidMarks");
			marks->set_mesh(marks_mesh);
			marks->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			marks->set_as_top_level(true);
			add_child(marks, false, INTERNAL_MODE_FRONT);
			marks->set_global_transform(Transform3D());
			next_segment = 0;
			used_segments = 0;
			marks_aabb_valid = false;
			segment_birth.resize(max_mark_segments);
			segment_alpha.resize(max_mark_segments);
			segment_shown.resize(max_mark_segments);
			for (int q = 0; q < max_mark_segments; q++) {
				segment_birth[q] = 0.0f;
				segment_alpha[q] = 0;
				segment_shown[q] = 0;
			}
		}
	}

	// The smoke: a particle system at each wheel, in the world, emitting as much as its tyre slides.
	if (smoke) {
		Ref<ParticleProcessMaterial> pm;
		pm.instantiate();
		pm->set_direction(Vector3(0, 1, 0));
		pm->set_spread(70.0f);
		pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.4f);
		pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 1.4f);
		pm->set_gravity(Vector3(0, 0.25f, 0));
		pm->set_param_min(ParticleProcessMaterial::PARAM_DAMPING, 0.8f);
		pm->set_param_max(ParticleProcessMaterial::PARAM_DAMPING, 1.4f);
		pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.5f);
		pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 0.9f);
		pm->set_param_min(ParticleProcessMaterial::PARAM_ANGLE, -180.0f);
		pm->set_param_max(ParticleProcessMaterial::PARAM_ANGLE, 180.0f);
		Ref<Curve> grow;
		grow.instantiate();
		grow->set_max_value(4.0f);
		grow->add_point(Vector2(0, 0.6f));
		grow->add_point(Vector2(1, 3.2f));
		Ref<CurveTexture> grow_tex;
		grow_tex.instantiate();
		grow_tex->set_curve(grow);
		pm->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, grow_tex);
		Ref<Gradient> fade;
		fade.instantiate();
		fade->set_color(0, Color(smoke_color.r, smoke_color.g, smoke_color.b, smoke_color.a));
		fade->set_color(1, Color(smoke_color.r, smoke_color.g, smoke_color.b, 0.0f));
		Ref<GradientTexture1D> fade_tex;
		fade_tex.instantiate();
		fade_tex->set_gradient(fade);
		pm->set_color_ramp(fade_tex);
		Ref<StandardMaterial3D> dm;
		dm.instantiate();
		dm->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		dm->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		dm->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
		dm->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		dm->set_proximity_fade_enabled(true);
		dm->set_proximity_fade_distance(0.5f);
		dm->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		Ref<QuadMesh> quad;
		quad.instantiate();
		quad->set_size(Vector2(0.8f, 0.8f));
		quad->set_material(dm);
		for (WheelFx &fx : wheels) {
			GPUParticles3D *p = memnew(GPUParticles3D);
			p->set_name("TyreSmoke");
			p->set_amount(smoke_amount);
			p->set_lifetime(smoke_lifetime);
			p->set_process_material(pm);
			p->set_draw_pass_mesh(0, quad);
			p->set_use_local_coordinates(false);
			p->set_emitting(false);
			p->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			p->set_visibility_aabb(AABB(Vector3(-20, -2, -20), Vector3(40, 12, 40)));
			p->set_as_top_level(true);
			add_child(p, false, INTERNAL_MODE_FRONT);
			fx.smoke = p;
		}
	}

	// Milestone 88: dust at each wheel on loose ground, thrown back and up and drifting, its colour the ground's.
	if (dust) {
		Ref<ParticleProcessMaterial> pm;
		pm.instantiate();
		pm->set_direction(Vector3(0, 1, 0));
		pm->set_spread(60.0f);
		pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.6f);
		pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 2.0f);
		pm->set_gravity(Vector3(0, -0.6f, 0));
		pm->set_param_min(ParticleProcessMaterial::PARAM_DAMPING, 1.0f);
		pm->set_param_max(ParticleProcessMaterial::PARAM_DAMPING, 2.0f);
		pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.6f);
		pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.0f);
		pm->set_param_min(ParticleProcessMaterial::PARAM_ANGLE, -180.0f);
		pm->set_param_max(ParticleProcessMaterial::PARAM_ANGLE, 180.0f);
		Ref<Curve> grow;
		grow.instantiate();
		grow->set_max_value(4.0f);
		grow->add_point(Vector2(0, 0.5f));
		grow->add_point(Vector2(1, 2.6f));
		Ref<CurveTexture> grow_tex;
		grow_tex.instantiate();
		grow_tex->set_curve(grow);
		pm->set_param_texture(ParticleProcessMaterial::PARAM_SCALE, grow_tex);
		dust_ramp.instantiate();
		dust_color_now = dust_color;
		dust_ramp->set_color(0, dust_color);
		dust_ramp->set_color(1, Color(dust_color.r, dust_color.g, dust_color.b, 0.0f));
		Ref<GradientTexture1D> ramp_tex;
		ramp_tex.instantiate();
		ramp_tex->set_gradient(dust_ramp);
		pm->set_color_ramp(ramp_tex);
		dust_material.instantiate();
		dust_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
		dust_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
		dust_material->set_billboard_mode(BaseMaterial3D::BILLBOARD_PARTICLES);
		dust_material->set_flag(BaseMaterial3D::FLAG_ALBEDO_FROM_VERTEX_COLOR, true);
		dust_material->set_proximity_fade_enabled(true);
		dust_material->set_proximity_fade_distance(0.5f);
		dust_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
		Ref<QuadMesh> quad;
		quad.instantiate();
		quad->set_size(Vector2(0.7f, 0.7f));
		quad->set_material(dust_material);
		for (WheelFx &fx : wheels) {
			GPUParticles3D *p = memnew(GPUParticles3D);
			p->set_name("TyreDust");
			p->set_amount(dust_amount);
			p->set_lifetime(dust_lifetime);
			p->set_process_material(pm);
			p->set_draw_pass_mesh(0, quad);
			p->set_use_local_coordinates(false);
			p->set_emitting(false);
			p->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
			p->set_visibility_aabb(AABB(Vector3(-20, -2, -20), Vector3(40, 10, 40)));
			p->set_as_top_level(true);
			add_child(p, false, INTERNAL_MODE_FRONT);
			fx.dust_fx = p;
		}
	}

	// The sound: a generator in a 3D player at the engine.
	generator.instantiate();
	sample_rate = AudioServer::get_singleton() ? AudioServer::get_singleton()->get_mix_rate() : 44100.0f;
	generator->set_mix_rate(sample_rate);
	generator->set_buffer_length(0.1f);
	audio = memnew(AudioStreamPlayer3D);
	audio->set_name("VehicleSound");
	audio->set_stream(generator);
	audio->set_unit_size(sound_unit_size);
	audio->set_position(engine_offset);
	add_child(audio, false, INTERNAL_MODE_FRONT);
	rng.seed(uint64_t(get_instance_id()));
	if (sound) {
		audio->play();
		playback = audio->get_stream_playback();
	}
	set_physics_process_internal(true);
	set_process_internal(true);
}

void FellowVehicleEffects3D::_teardown() {
	set_physics_process_internal(false);
	set_process_internal(false);
	for (WheelFx &fx : wheels) {
		if (fx.smoke) {
			fx.smoke->queue_free();
			fx.smoke = nullptr;
		}
		if (fx.dust_fx) {
			fx.dust_fx->queue_free();
			fx.dust_fx = nullptr;
		}
	}
	wheels.clear();
	dust_material.unref();
	dust_ramp.unref();
	if (marks) {
		marks->queue_free();
		marks = nullptr;
	}
	marks_mesh.unref();
	marks_material.unref();
	playback.unref();
	if (audio) {
		audio->stop(); // its playback released now (a player still playing when the game quits keeps it)
		audio->queue_free();
		audio = nullptr;
	}
	generator.unref();
	vehicle = nullptr;
}

void FellowVehicleEffects3D::clear_marks() {
	if (marks_mesh.is_null()) {
		return;
	}
	const int nv = max_mark_segments * 4;
	Vector<uint8_t> ab;
	ab.resize(nv * attribute_stride);
	memset(ab.ptrw(), 0, ab.size());
	marks_mesh->surface_update_attribute_region(0, 0, ab);
	next_segment = 0;
	used_segments = 0;
	for (uint32_t q = 0; q < segment_shown.size(); q++) {
		segment_alpha[q] = 0;
		segment_shown[q] = 0;
	}
}

// Milestone 88: marks older than half their lifetime fade out by its end (the segments' colours written again in one
// update, four times a second, when one changed).
void FellowVehicleEffects3D::_fade_marks() {
	if (marks_mesh.is_null() || mark_lifetime <= 0.0f || used_segments == 0) {
		return;
	}
	const float half = mark_lifetime * 0.5f;
	bool changed = false;
	for (uint32_t q = 0; q < used_segments; q++) {
		const float age = float(clock) - segment_birth[q];
		const float k = age <= half ? 1.0f : CLAMP(1.0f - (age - half) / half, 0.0f, 1.0f);
		const uint8_t a = uint8_t(float(segment_alpha[q]) * k);
		if (a != segment_shown[q]) {
			segment_shown[q] = a;
			changed = true;
		}
	}
	if (!changed) {
		return;
	}
	const int nv = max_mark_segments * 4;
	Vector<uint8_t> ab;
	ab.resize(nv * attribute_stride);
	memset(ab.ptrw(), 0, ab.size());
	for (uint32_t q = 0; q < used_segments; q++) {
		for (int i = 0; i < 4; i++) {
			uint8_t *c = ab.ptrw() + (q * 4 + i) * attribute_stride + color_offset;
			c[0] = 255;
			c[1] = 255;
			c[2] = 255;
			c[3] = segment_shown[q];
		}
	}
	marks_mesh->surface_update_attribute_region(0, 0, ab);
}

void FellowVehicleEffects3D::_add_segment(const Vector3 &p_l0, const Vector3 &p_r0, const Vector3 &p_l1, const Vector3 &p_r1, float p_alpha) {
	const uint32_t base = next_segment * 4;
	const Vector3 pts[4] = { p_l0, p_r0, p_l1, p_r1 };
	Vector<uint8_t> vb;
	vb.resize(4 * vertex_stride);
	float *vf = reinterpret_cast<float *>(vb.ptrw());
	for (int i = 0; i < 4; i++) {
		vf[i * 3 + 0] = pts[i].x;
		vf[i * 3 + 1] = pts[i].y;
		vf[i * 3 + 2] = pts[i].z;
	}
	Vector<uint8_t> ab;
	ab.resize(4 * attribute_stride);
	memset(ab.ptrw(), 0, ab.size());
	const uint8_t a = uint8_t(CLAMP(p_alpha * 255.0f, 0.0f, 255.0f));
	for (int i = 0; i < 4; i++) {
		uint8_t *c = ab.ptrw() + i * attribute_stride + color_offset;
		c[0] = 255;
		c[1] = 255;
		c[2] = 255;
		c[3] = a;
	}
	marks_mesh->surface_update_vertex_region(0, base * vertex_stride, vb);
	marks_mesh->surface_update_attribute_region(0, base * attribute_stride, ab);
	if (next_segment < segment_birth.size()) {
		segment_birth[next_segment] = float(clock);
		segment_alpha[next_segment] = a;
		segment_shown[next_segment] = a;
	}
	next_segment = (next_segment + 1) % uint32_t(max_mark_segments);
	used_segments = MIN(used_segments + 1, uint32_t(max_mark_segments));
	if (segments_laid == 0) {
		mark_low = p_l0.y;
		mark_high = p_l0.y;
	}
	for (const Vector3 &q : pts) {
		mark_low = MIN(mark_low, q.y);
		mark_high = MAX(mark_high, q.y);
	}
	segments_laid++;
	global_stats.segments++;
	// The bounds the marks have reached (the mesh is in the world, at the origin).
	AABB box(p_l0, Vector3());
	box.expand_to(p_r0);
	box.expand_to(p_l1);
	box.expand_to(p_r1);
	if (!marks_aabb_valid || !marks_aabb.encloses(box)) {
		marks_aabb = marks_aabb_valid ? marks_aabb.merge(box.grow(8.0f)) : box.grow(8.0f);
		marks_aabb_valid = true;
		marks_mesh->set_custom_aabb(marks_aabb);
	}
}

void FellowVehicleEffects3D::_physics_step() {
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	clock += get_physics_process_delta_time();
	const float ground_speed = vehicle ? Vector3(vehicle->get_linear_velocity().x, 0.0f, vehicle->get_linear_velocity().z).length() : 0.0f;
	for (WheelFx &fx : wheels) {
		FellowWheel3D *w = ObjectDB::get_instance<FellowWheel3D>(fx.wheel);
		if (!w) {
			continue;
		}
		const bool contact = w->is_in_contact();
		// Sliding: fast enough over the ground, and past the tyre's grip (its combined slip beyond slide_slip: a tyre
		// held at its peak by the traction control or the anti-lock brakes marks nothing).
		float intensity = 0.0f;
		if (contact) {
			const float sx = w->get_slip_ratio();
			const float sy = Math::tan(CLAMP(w->get_slip_angle(), -1.4f, 1.4f));
			const float combined = Math::sqrt(sx * sx + sy * sy);
			intensity = CLAMP((w->get_slip_speed() - slip_min) / MAX(slip_full - slip_min, 0.01f), 0.0f, 1.0f) * CLAMP((combined - slide_slip) / 0.2f, 0.0f, 1.0f);
		}
		fx.intensity = intensity;
		// Milestone 88: loose ground (a collider in loose_ground_group; its metadata "fellow_dust_color" the dust's
		// colour): dust as the tyre rolls (more with the speed, and with its sliding), ruts where it rolls.
		fx.loose = false;
		if (contact) {
			if (const Node *g = Object::cast_to<Node>(ObjectDB::get_instance(w->get_ground_id()))) {
				fx.loose = g->is_in_group(loose_ground_group);
				if (fx.loose && g->has_meta(SNAME("fellow_dust_color")) && dust_ramp.is_valid()) {
					const Color c = g->get_meta(SNAME("fellow_dust_color"));
					if (c != dust_color_now) {
						dust_color_now = c;
						dust_ramp->set_color(0, c);
						dust_ramp->set_color(1, Color(c.r, c.g, c.b, 0.0f));
					}
				}
			}
		}
		fx.dust_intensity = fx.loose ? MAX(CLAMP((ground_speed - dust_min_speed) / MAX(dust_full_speed - dust_min_speed, 0.01f), 0.0f, 1.0f), intensity) : 0.0f;
		const float rut = (fx.loose && ground_speed > 0.5f) ? rut_opacity : 0.0f;
		const float mark_alpha = MAX(fx.intensity * mark_opacity, rut);
		const Vector3 n = contact ? w->get_contact_normal() : Vector3(0, 1, 0);
		const Vector3 at = contact ? w->get_contact_point() : w->get_global_position();
		if (marks_mesh.is_valid() && skid_marks && mark_alpha > 0.0f) {
			// Across the tyre on the ground (the wheel's axle, its X), a little above it.
			Vector3 axle = w->get_global_transform().basis.get_column(0);
			axle -= n * axle.dot(n);
			axle = axle.length_squared() > 1e-8f ? axle.normalized() : Vector3(1, 0, 0);
			const Vector3 p = at + n * 0.02f;
			const Vector3 l = p - axle * (mark_width * 0.5f);
			const Vector3 r = p + axle * (mark_width * 0.5f);
			if (!fx.laying) {
				fx.left = l;
				fx.right = r;
				fx.laying = true;
			} else if (p.distance_to((fx.left + fx.right) * 0.5f) >= mark_step) {
				_add_segment(fx.left, fx.right, l, r, mark_alpha);
				fx.left = l;
				fx.right = r;
			}
		} else {
			fx.laying = false;
		}
		if (fx.smoke) {
			fx.smoke->set_global_position(at + n * 0.1f);
			const bool on = smoke && fx.intensity > 0.05f && !fx.loose; // on loose ground it raises dust instead
			if (on) {
				fx.smoke->set_amount_ratio(fx.intensity);
			}
			if (fx.smoke->is_emitting() != on) {
				fx.smoke->set_emitting(on);
			}
		}
		if (fx.dust_fx) {
			fx.dust_fx->set_global_position(at + n * 0.15f);
			const bool on = dust && fx.dust_intensity > 0.02f;
			if (on) {
				fx.dust_fx->set_amount_ratio(fx.dust_intensity);
			}
			if (fx.dust_fx->is_emitting() != on) {
				fx.dust_fx->set_emitting(on);
			}
		}
	}
	fade_timer += get_physics_process_delta_time();
	if (fade_timer >= 0.25) {
		fade_timer = 0.0;
		_fade_marks();
	}
	last_usec = OS::get_singleton()->get_ticks_usec() - t0;
	global_stats.frames++;
	global_stats.usec += last_usec;
}

// The sound, synthesised for what the generator can take: the engine's firing (its frequency and five harmonics, a
// burst of noise at each firing, low-passed brighter with the throttle and the revolutions, the crankshaft's rumble
// below), the tyres' squeal (noise through a resonant band-pass rising with their sliding), the road's rumble
// (low-passed noise with the speed squared). The controls eased over 30 ms.
void FellowVehicleEffects3D::_synthesise() {
	if (!vehicle || !audio || !sound) {
		return;
	}
	// Silent (stopped) farther than sound_distance from the listener: nothing synthesised for it.
	if (Viewport *vp = get_viewport()) {
		Node3D *listener = vp->get_audio_listener_3d();
		if (!listener) {
			listener = vp->get_camera_3d();
		}
		if (listener) {
			const bool near = listener->get_global_position().distance_to(audio->get_global_position()) <= sound_distance;
			if (!near && audio->is_playing()) {
				audio->stop();
				playback.unref();
			} else if (near && !audio->is_playing()) {
				audio->play();
				playback = audio->get_stream_playback();
			}
		}
	}
	if (playback.is_null()) {
		return;
	}
	const uint64_t t0 = OS::get_singleton()->get_ticks_usec();
	const int frames = playback->get_frames_available();
	if (frames <= 0) {
		return;
	}
	const float max_rpm = MAX(vehicle->get_max_rpm(), 1000.0f);
	const float t_rpm = vehicle->get_engine_rpm();
	const float t_load = vehicle->get_throttle();
	float t_squeal = 0.0f;
	float slide = 0.0f;
	for (const WheelFx &fx : wheels) {
		t_squeal = MAX(t_squeal, fx.intensity);
		if (FellowWheel3D *w = ObjectDB::get_instance<FellowWheel3D>(fx.wheel)) {
			slide = MAX(slide, w->get_slip_speed());
		}
	}
	const float t_pitch = CLAMP(slide / 15.0f, 0.0f, 1.0f);
	const float t_speed = Math::abs(vehicle->get_speed());
	if (s_rpm <= 0.0f) {
		s_rpm = t_rpm;
	}
	const float engine_amp = Math::db_to_linear(engine_volume_db) * 0.35f;
	const float tyre_amp = Math::db_to_linear(tyre_volume_db) * 0.5f;
	const float road_amp = Math::db_to_linear(road_volume_db) * 0.5f;
	const float ease = 1.0f - Math::exp(-1.0f / (0.03f * sample_rate));
	// The squeal's band-pass (RBJ, its peak at 0 dB), its centre from 800 to 1 500 Hz, set for this block.
	squeal_pitch_now += (t_pitch - squeal_pitch_now) * 0.3f;
	const float f_sq = 800.0f + 700.0f * squeal_pitch_now;
	const float w0 = float(Math::TAU) * f_sq / sample_rate;
	const float alpha = Math::sin(w0) / (2.0f * 8.0f);
	const float a0 = 1.0f + alpha;
	const float b0 = alpha / a0;
	const float b2 = -alpha / a0;
	const float a1 = -2.0f * Math::cos(w0) / a0;
	const float a2 = (1.0f - alpha) / a0;
	const float road_k = 1.0f - Math::exp(-float(Math::TAU) * 400.0f / sample_rate);
	const float harm[5] = { 1.0f, 0.5f, 0.33f, 0.25f, 0.2f };
	// The engine's low-pass for this block (from the controls it is heading to: they ease over 30 ms).
	const float fc = 200.0f + 2500.0f * (0.3f + 0.7f * CLAMP(t_load, 0.0f, 1.0f)) * CLAMP(t_rpm / max_rpm, 0.0f, 1.0f);
	const float engine_k = 1.0f - Math::exp(-float(Math::TAU) * fc / sample_rate);

	PackedVector2Array buf;
	buf.resize(frames);
	Vector2 *out = buf.ptrw();
	for (int i = 0; i < frames; i++) {
		s_rpm += (t_rpm - s_rpm) * ease;
		s_load += (t_load - s_load) * ease;
		s_squeal += (t_squeal - s_squeal) * ease;
		s_speed += (t_speed - s_speed) * ease;
		const float rev = CLAMP(s_rpm / max_rpm, 0.0f, 1.0f);
		const float f_fire = s_rpm / 60.0f * float(cylinders) * 0.5f;
		phase += f_fire / sample_rate;
		phase -= Math::floor(phase);
		crank += s_rpm / 60.0f / sample_rate;
		crank -= Math::floor(crank);
		// The harmonics by Chebyshev's recurrence from the fundamental's sine and cosine.
		const float th = float(Math::TAU) * phase;
		const float s1 = Math::sin(th);
		const float c1 = Math::cos(th);
		float sk_1 = 0.0f;
		float sk = s1;
		float tone = 0.0f;
		for (int k = 0; k < 5; k++) {
			const float bright = k < 2 ? 1.0f : (0.3f + 0.7f * s_load);
			tone += harm[k] * bright * sk;
			const float next = 2.0f * c1 * sk - sk_1;
			sk_1 = sk;
			sk = next;
		}
		tone += 0.4f * Math::sin(float(Math::TAU) * crank);
		const float e1 = 0.5f + 0.5f * c1;
		const float e2 = e1 * e1;
		const float env = e2 * e2 * e2;
		const float white = rng.randf() * 2.0f - 1.0f;
		float engine = 0.5f * tone + (0.3f + 0.7f * s_load) * 0.6f * env * white;
		lp_engine += (engine - lp_engine) * engine_k;
		engine = lp_engine * (0.15f + 0.85f * s_load) * (0.4f + 0.6f * rev) * engine_amp;
		// The squeal.
		const float x = rng.randf() * 2.0f - 1.0f;
		const float y = b0 * x + b2 * bp_x2 - a1 * bp_y1 - a2 * bp_y2;
		bp_x2 = bp_x1;
		bp_x1 = x;
		bp_y2 = bp_y1;
		bp_y1 = y;
		const float squeal = y * 4.0f * s_squeal * tyre_amp;
		// The road.
		const float rn = rng.randf() * 2.0f - 1.0f;
		lp_road += (rn - lp_road) * road_k;
		const float sp = MIN(s_speed / 40.0f, 1.5f);
		const float road = lp_road * 3.0f * sp * sp * road_amp;
		// A soft limit (tanh's Padé approximant, exact enough under 3).
		const float mix = CLAMP(engine + squeal + road, -3.0f, 3.0f);
		const float v = mix * (27.0f + mix * mix) / (27.0f + 9.0f * mix * mix);
		out[i] = Vector2(v, v);
	}
	playback->push_buffer(buf);
	last_synth_usec = OS::get_singleton()->get_ticks_usec() - t0;
	global_stats.synth_usec += last_synth_usec;
	global_stats.samples += uint64_t(frames);
}

void FellowVehicleEffects3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				_setup();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (!Engine::get_singleton()->is_editor_hint()) {
				_teardown();
			}
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			_physics_step();
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (sound && audio && playback.is_null() && audio->is_playing()) {
				playback = audio->get_stream_playback();
			}
			_synthesise();
		} break;
	}
}

Dictionary FellowVehicleEffects3D::get_stats() const {
	Dictionary d;
	d["mark_segments"] = int(used_segments);
	d["segments_laid"] = int64_t(segments_laid);
	d["mark_low"] = mark_low;
	d["mark_high"] = mark_high;
	d["marks_aabb"] = marks_aabb;
	Array ws;
	int smoking = 0;
	for (const WheelFx &fx : wheels) {
		ws.push_back(fx.intensity);
		smoking += fx.smoke && fx.smoke->is_emitting() ? 1 : 0;
	}
	d["intensity"] = ws;
	d["smoking"] = smoking;
	int dusting = 0;
	int loose = 0;
	for (const WheelFx &fx : wheels) {
		dusting += fx.dust_fx && fx.dust_fx->is_emitting() ? 1 : 0;
		loose += fx.loose ? 1 : 0;
	}
	d["dusting"] = dusting;
	d["on_loose_ground"] = loose;
	int visible = 0;
	for (uint32_t q = 0; q < used_segments && q < segment_shown.size(); q++) {
		visible += segment_shown[q] > 0 ? 1 : 0;
	}
	d["visible_segments"] = visible;
	d["usec"] = int64_t(last_usec);
	d["synth_usec"] = int64_t(last_synth_usec);
	d["sound_rpm"] = s_rpm;
	return d;
}

Dictionary FellowVehicleEffects3D::get_global_stats() {
	Dictionary d;
	d["frames"] = int64_t(global_stats.frames);
	d["usec"] = int64_t(global_stats.usec);
	d["segments"] = int64_t(global_stats.segments);
	d["synth_usec"] = int64_t(global_stats.synth_usec);
	d["samples"] = int64_t(global_stats.samples);
	return d;
}

void FellowVehicleEffects3D::reset_global_stats() {
	global_stats = GlobalStats();
}

PackedStringArray FellowVehicleEffects3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!Object::cast_to<FellowVehicle3D>(get_parent())) {
		warnings.push_back(RTR("A FellowVehicleEffects3D shows and sounds its parent, which must be a FellowVehicle3D."));
	}
	return warnings;
}

void FellowVehicleEffects3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_skid_marks", "enabled"), &FellowVehicleEffects3D::set_skid_marks);
	ClassDB::bind_method(D_METHOD("has_skid_marks"), &FellowVehicleEffects3D::has_skid_marks);
	ClassDB::bind_method(D_METHOD("set_max_mark_segments", "count"), &FellowVehicleEffects3D::set_max_mark_segments);
	ClassDB::bind_method(D_METHOD("get_max_mark_segments"), &FellowVehicleEffects3D::get_max_mark_segments);
	ClassDB::bind_method(D_METHOD("set_mark_width", "width"), &FellowVehicleEffects3D::set_mark_width);
	ClassDB::bind_method(D_METHOD("get_mark_width"), &FellowVehicleEffects3D::get_mark_width);
	ClassDB::bind_method(D_METHOD("set_mark_step", "step"), &FellowVehicleEffects3D::set_mark_step);
	ClassDB::bind_method(D_METHOD("get_mark_step"), &FellowVehicleEffects3D::get_mark_step);
	ClassDB::bind_method(D_METHOD("set_mark_opacity", "opacity"), &FellowVehicleEffects3D::set_mark_opacity);
	ClassDB::bind_method(D_METHOD("get_mark_opacity"), &FellowVehicleEffects3D::get_mark_opacity);
	ClassDB::bind_method(D_METHOD("set_slip_min", "speed"), &FellowVehicleEffects3D::set_slip_min);
	ClassDB::bind_method(D_METHOD("get_slip_min"), &FellowVehicleEffects3D::get_slip_min);
	ClassDB::bind_method(D_METHOD("set_slip_full", "speed"), &FellowVehicleEffects3D::set_slip_full);
	ClassDB::bind_method(D_METHOD("get_slip_full"), &FellowVehicleEffects3D::get_slip_full);
	ClassDB::bind_method(D_METHOD("set_slide_slip", "slip"), &FellowVehicleEffects3D::set_slide_slip);
	ClassDB::bind_method(D_METHOD("get_slide_slip"), &FellowVehicleEffects3D::get_slide_slip);
	ClassDB::bind_method(D_METHOD("set_smoke", "enabled"), &FellowVehicleEffects3D::set_smoke);
	ClassDB::bind_method(D_METHOD("has_smoke"), &FellowVehicleEffects3D::has_smoke);
	ClassDB::bind_method(D_METHOD("set_smoke_amount", "amount"), &FellowVehicleEffects3D::set_smoke_amount);
	ClassDB::bind_method(D_METHOD("get_smoke_amount"), &FellowVehicleEffects3D::get_smoke_amount);
	ClassDB::bind_method(D_METHOD("set_smoke_lifetime", "lifetime"), &FellowVehicleEffects3D::set_smoke_lifetime);
	ClassDB::bind_method(D_METHOD("get_smoke_lifetime"), &FellowVehicleEffects3D::get_smoke_lifetime);
	ClassDB::bind_method(D_METHOD("set_smoke_color", "color"), &FellowVehicleEffects3D::set_smoke_color);
	ClassDB::bind_method(D_METHOD("get_smoke_color"), &FellowVehicleEffects3D::get_smoke_color);
	ClassDB::bind_method(D_METHOD("set_sound", "enabled"), &FellowVehicleEffects3D::set_sound);
	ClassDB::bind_method(D_METHOD("has_sound"), &FellowVehicleEffects3D::has_sound);
	ClassDB::bind_method(D_METHOD("set_cylinders", "count"), &FellowVehicleEffects3D::set_cylinders);
	ClassDB::bind_method(D_METHOD("get_cylinders"), &FellowVehicleEffects3D::get_cylinders);
	ClassDB::bind_method(D_METHOD("set_engine_volume_db", "db"), &FellowVehicleEffects3D::set_engine_volume_db);
	ClassDB::bind_method(D_METHOD("get_engine_volume_db"), &FellowVehicleEffects3D::get_engine_volume_db);
	ClassDB::bind_method(D_METHOD("set_tyre_volume_db", "db"), &FellowVehicleEffects3D::set_tyre_volume_db);
	ClassDB::bind_method(D_METHOD("get_tyre_volume_db"), &FellowVehicleEffects3D::get_tyre_volume_db);
	ClassDB::bind_method(D_METHOD("set_road_volume_db", "db"), &FellowVehicleEffects3D::set_road_volume_db);
	ClassDB::bind_method(D_METHOD("get_road_volume_db"), &FellowVehicleEffects3D::get_road_volume_db);
	ClassDB::bind_method(D_METHOD("set_engine_offset", "offset"), &FellowVehicleEffects3D::set_engine_offset);
	ClassDB::bind_method(D_METHOD("get_engine_offset"), &FellowVehicleEffects3D::get_engine_offset);
	ClassDB::bind_method(D_METHOD("set_sound_distance", "distance"), &FellowVehicleEffects3D::set_sound_distance);
	ClassDB::bind_method(D_METHOD("get_sound_distance"), &FellowVehicleEffects3D::get_sound_distance);
	ClassDB::bind_method(D_METHOD("set_sound_unit_size", "size"), &FellowVehicleEffects3D::set_sound_unit_size);
	ClassDB::bind_method(D_METHOD("get_sound_unit_size"), &FellowVehicleEffects3D::get_sound_unit_size);
	ClassDB::bind_method(D_METHOD("set_loose_ground_group", "group"), &FellowVehicleEffects3D::set_loose_ground_group);
	ClassDB::bind_method(D_METHOD("get_loose_ground_group"), &FellowVehicleEffects3D::get_loose_ground_group);
	ClassDB::bind_method(D_METHOD("set_dust", "enabled"), &FellowVehicleEffects3D::set_dust);
	ClassDB::bind_method(D_METHOD("has_dust"), &FellowVehicleEffects3D::has_dust);
	ClassDB::bind_method(D_METHOD("set_dust_amount", "amount"), &FellowVehicleEffects3D::set_dust_amount);
	ClassDB::bind_method(D_METHOD("get_dust_amount"), &FellowVehicleEffects3D::get_dust_amount);
	ClassDB::bind_method(D_METHOD("set_dust_lifetime", "lifetime"), &FellowVehicleEffects3D::set_dust_lifetime);
	ClassDB::bind_method(D_METHOD("get_dust_lifetime"), &FellowVehicleEffects3D::get_dust_lifetime);
	ClassDB::bind_method(D_METHOD("set_dust_color", "color"), &FellowVehicleEffects3D::set_dust_color);
	ClassDB::bind_method(D_METHOD("get_dust_color"), &FellowVehicleEffects3D::get_dust_color);
	ClassDB::bind_method(D_METHOD("set_dust_min_speed", "speed"), &FellowVehicleEffects3D::set_dust_min_speed);
	ClassDB::bind_method(D_METHOD("get_dust_min_speed"), &FellowVehicleEffects3D::get_dust_min_speed);
	ClassDB::bind_method(D_METHOD("set_dust_full_speed", "speed"), &FellowVehicleEffects3D::set_dust_full_speed);
	ClassDB::bind_method(D_METHOD("get_dust_full_speed"), &FellowVehicleEffects3D::get_dust_full_speed);
	ClassDB::bind_method(D_METHOD("set_rut_opacity", "opacity"), &FellowVehicleEffects3D::set_rut_opacity);
	ClassDB::bind_method(D_METHOD("get_rut_opacity"), &FellowVehicleEffects3D::get_rut_opacity);
	ClassDB::bind_method(D_METHOD("set_mark_lifetime", "seconds"), &FellowVehicleEffects3D::set_mark_lifetime);
	ClassDB::bind_method(D_METHOD("get_mark_lifetime"), &FellowVehicleEffects3D::get_mark_lifetime);
	ClassDB::bind_method(D_METHOD("clear_marks"), &FellowVehicleEffects3D::clear_marks);
	ClassDB::bind_method(D_METHOD("get_audio_player"), &FellowVehicleEffects3D::get_audio_player);
	ClassDB::bind_method(D_METHOD("get_stats"), &FellowVehicleEffects3D::get_stats);
	ClassDB::bind_static_method("FellowVehicleEffects3D", D_METHOD("get_global_stats"), &FellowVehicleEffects3D::get_global_stats);
	ClassDB::bind_static_method("FellowVehicleEffects3D", D_METHOD("reset_global_stats"), &FellowVehicleEffects3D::reset_global_stats);

	ADD_GROUP("Skid Marks", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "skid_marks"), "set_skid_marks", "has_skid_marks");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_mark_segments", PROPERTY_HINT_RANGE, "8,16384,1"), "set_max_mark_segments", "get_max_mark_segments");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mark_width", PROPERTY_HINT_RANGE, "0.01,1,0.01,suffix:m"), "set_mark_width", "get_mark_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mark_step", PROPERTY_HINT_RANGE, "0.05,2,0.01,suffix:m"), "set_mark_step", "get_mark_step");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mark_opacity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_mark_opacity", "get_mark_opacity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slip_min", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:m/s"), "set_slip_min", "get_slip_min");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slip_full", PROPERTY_HINT_RANGE, "0.01,30,0.01,suffix:m/s"), "set_slip_full", "get_slip_full");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slide_slip", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_slide_slip", "get_slide_slip");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "mark_lifetime", PROPERTY_HINT_RANGE, "0,600,0.1,or_greater,suffix:s"), "set_mark_lifetime", "get_mark_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rut_opacity", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_rut_opacity", "get_rut_opacity");
	ADD_GROUP("Smoke", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "smoke"), "set_smoke", "has_smoke");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "smoke_amount", PROPERTY_HINT_RANGE, "1,512,1"), "set_smoke_amount", "get_smoke_amount");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoke_lifetime", PROPERTY_HINT_RANGE, "0.1,10,0.01,suffix:s"), "set_smoke_lifetime", "get_smoke_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "smoke_color"), "set_smoke_color", "get_smoke_color");
	ADD_GROUP("Dust", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "dust"), "set_dust", "has_dust");
	ADD_PROPERTY(PropertyInfo(Variant::STRING_NAME, "loose_ground_group"), "set_loose_ground_group", "get_loose_ground_group");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "dust_amount", PROPERTY_HINT_RANGE, "1,512,1"), "set_dust_amount", "get_dust_amount");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dust_lifetime", PROPERTY_HINT_RANGE, "0.1,10,0.01,suffix:s"), "set_dust_lifetime", "get_dust_lifetime");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "dust_color"), "set_dust_color", "get_dust_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dust_min_speed", PROPERTY_HINT_RANGE, "0,30,0.01,suffix:m/s"), "set_dust_min_speed", "get_dust_min_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "dust_full_speed", PROPERTY_HINT_RANGE, "0.01,60,0.01,suffix:m/s"), "set_dust_full_speed", "get_dust_full_speed");
	ADD_GROUP("Sound", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sound"), "set_sound", "has_sound");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "cylinders", PROPERTY_HINT_RANGE, "1,16,1"), "set_cylinders", "get_cylinders");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "engine_volume_db", PROPERTY_HINT_RANGE, "-60,12,0.1,suffix:dB"), "set_engine_volume_db", "get_engine_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "tyre_volume_db", PROPERTY_HINT_RANGE, "-60,12,0.1,suffix:dB"), "set_tyre_volume_db", "get_tyre_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "road_volume_db", PROPERTY_HINT_RANGE, "-60,12,0.1,suffix:dB"), "set_road_volume_db", "get_road_volume_db");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "engine_offset", PROPERTY_HINT_NONE, "suffix:m"), "set_engine_offset", "get_engine_offset");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_unit_size", PROPERTY_HINT_RANGE, "0.1,100,0.1"), "set_sound_unit_size", "get_sound_unit_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "sound_distance", PROPERTY_HINT_RANGE, "1,1000,0.1,or_greater,suffix:m"), "set_sound_distance", "get_sound_distance");
}

FellowVehicleEffects3D::FellowVehicleEffects3D() {
}

FellowVehicleEffects3D::~FellowVehicleEffects3D() {
}
