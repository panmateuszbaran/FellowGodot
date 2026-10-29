/**************************************************************************/
/*  fellow_rain_3d.cpp                                                    */
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

#include "fellow_rain_3d.h"

#include "fellow_rain.glsl.gen.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/particle_process_material.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/renderer_rd/effects/fellow_weather.h"
#include "servers/rendering/rendering_server.h"

FellowRain3D *FellowRain3D::active = nullptr;

// ------------------------------------------------------------------ children

static GPUParticlesCollisionHeightField3D::Resolution rain_occlusion_resolution(int p_res) {
	switch (p_res) {
		case 256:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_256;
		case 512:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_512;
		case 2048:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_2048;
		case 4096:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_4096;
		default:
			return GPUParticlesCollisionHeightField3D::RESOLUTION_1024;
	}
}

void FellowRain3D::_build_children() {
	if (occlusion) {
		return;
	}
	occlusion = memnew(GPUParticlesCollisionHeightField3D);
	occlusion->set_name("RainOcclusion");
	occlusion->set_as_top_level(true);
	add_child(occlusion, false, INTERNAL_MODE_FRONT);

	drop_process.instantiate();
	drop_process->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
	drop_process->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.8);
	drop_process->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.2);
	drop_process->set_spread(0.0);
	drop_process->set_gravity(Vector3());
	drop_process->set_particle_flag(ParticleProcessMaterial::PARTICLE_FLAG_ALIGN_Y_TO_VELOCITY, true);
	drop_process->set_collision_mode(ParticleProcessMaterial::COLLISION_HIDE_ON_CONTACT);

	drop_material.instantiate();
	drop_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	drop_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	drop_material->set_billboard_mode(BaseMaterial3D::BILLBOARD_FIXED_Y);
	drop_material->set_flag(BaseMaterial3D::FLAG_DISABLE_FOG, false);
	drop_material->set_cull_mode(BaseMaterial3D::CULL_DISABLED);
	// A drop passing the lens would fill the screen (and be out of focus): the drops fade in from 0.5 m to 4 m from
	// the camera.
	drop_material->set_distance_fade(BaseMaterial3D::DISTANCE_FADE_PIXEL_ALPHA);
	drop_material->set_distance_fade_min_distance(0.5);
	drop_material->set_distance_fade_max_distance(4.0);
	// A streak, not a rectangle: soft across, thinning to both ends (the mipmaps average a distant one to a faint line).
	{
		const int w = 8;
		const int h = 64;
		Ref<Image> img = Image::create_empty(w, h, false, Image::FORMAT_RGBA8);
		for (int y = 0; y < h; y++) {
			float v = (y + 0.5f) / h;
			float along = Math::pow(Math::sin(v * float(Math::PI)), 0.6f);
			for (int x = 0; x < w; x++) {
				float u = ((x + 0.5f) / w) * 2.0f - 1.0f;
				float across = 1.0f - u * u;
				img->set_pixel(x, y, Color(1.0f, 1.0f, 1.0f, across * along));
			}
		}
		img->generate_mipmaps();
		drop_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, ImageTexture::create_from_image(img));
	}

	Ref<QuadMesh> drop_mesh;
	drop_mesh.instantiate();
	drop_mesh->set_material(drop_material);

	drops = memnew(GPUParticles3D);
	drops->set_name("RainDrops");
	drops->set_as_top_level(true);
	drops->set_use_local_coordinates(false);
	drops->set_process_material(drop_process);
	drops->set_draw_pass_mesh(0, drop_mesh);
	drops->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	drops->set_collision_base_size(0.02);
	drops->set_use_fixed_seed(true);
	drops->set_seed(20261029);
	add_child(drops, false, INTERNAL_MODE_FRONT);

	splash_process.instantiate();
	splash_process->set_direction(Vector3(0, 1, 0));
	splash_process->set_spread(40.0);
	splash_process->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.6);
	splash_process->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 1.4);
	splash_process->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.5);
	splash_process->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.0);
	splash_process->set_gravity(Vector3(0, -9.8, 0));
	// A drop is stopped a step below the surface it hits (hide on contact does not move it out) and its splash is
	// emitted there: colliding, the splash is put back on the surface (a roof's splashes stay above its ceiling).
	splash_process->set_collision_mode(ParticleProcessMaterial::COLLISION_RIGID);
	splash_process->set_collision_bounce(0.1);
	splash_process->set_collision_friction(0.5);

	splash_material.instantiate();
	splash_material->set_shading_mode(BaseMaterial3D::SHADING_MODE_UNSHADED);
	splash_material->set_transparency(BaseMaterial3D::TRANSPARENCY_ALPHA);
	splash_material->set_billboard_mode(BaseMaterial3D::BILLBOARD_ENABLED);

	Ref<QuadMesh> splash_mesh;
	splash_mesh.instantiate();
	splash_mesh->set_size(Vector2(0.018, 0.018));
	splash_mesh->set_material(splash_material);

	splash = memnew(GPUParticles3D);
	splash->set_name("RainSplashes");
	splash->set_as_top_level(true);
	splash->set_use_local_coordinates(false);
	splash->set_process_material(splash_process);
	splash->set_draw_pass_mesh(0, splash_mesh);
	splash->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	splash->set_lifetime(0.3);
	splash->set_collision_base_size(0.01);
	splash->set_emitting(false);
	splash->set_use_fixed_seed(true);
	splash->set_seed(20261030);
	add_child(splash, false, INTERNAL_MODE_FRONT);
	_update_children();
}

void FellowRain3D::_update_children() {
	if (!occlusion) {
		return;
	}
	occlusion->set_size(Vector3(occlusion_size, occlusion_height, occlusion_size));
	occlusion->set_resolution(rain_occlusion_resolution(occlusion_resolution));
	// FellowRain3D moves it with the camera itself (by whole sixteenths of its size: whole texels, so the shelter's
	// edges do not shift between renders, and five times fewer renders than the stock 1 m steps).
	occlusion->set_follow_camera_enabled(false);
	occlusion->set_update_mode(occlusion_always_update ? GPUParticlesCollisionHeightField3D::UPDATE_MODE_ALWAYS : GPUParticlesCollisionHeightField3D::UPDATE_MODE_WHEN_MOVED);

	Vector3 v = wind - Vector3(0, drop_speed, 0);
	float speed = v.length();
	drop_process->set_direction(v / MAX(speed, 0.01f));
	drop_process->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, speed * 0.95);
	drop_process->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, speed * 1.05);
	drop_process->set_emission_box_extents(Vector3(area * 0.5f, 0.5f, area * 0.5f));
	drop_process->set_sub_emitter_mode(splashes ? ParticleProcessMaterial::SUB_EMITTER_AT_COLLISION : ParticleProcessMaterial::SUB_EMITTER_DISABLED);
	drop_process->set_sub_emitter_amount_at_collision(2);
	drop_material->set_albedo(drop_color);
	splash_material->set_albedo(Color(drop_color.r, drop_color.g, drop_color.b, MIN(1.0f, drop_color.a * 2.0f)));
	Ref<QuadMesh> qm = drops->get_draw_pass_mesh(0);
	if (qm.is_valid()) {
		qm->set_size(drop_size);
	}
	// The drops fall from fall_height above the camera to well below it (the ground may be far below).
	double lifetime = (fall_height + 30.0) / MAX(drop_speed, 0.1f);
	if (drops->get_amount() != max_drops) {
		drops->set_amount(max_drops);
	}
	if (!Math::is_equal_approx(drops->get_lifetime(), lifetime)) {
		drops->set_lifetime(lifetime);
		drops->set_pre_process_time(lifetime);
	}
	drops->set_visibility_aabb(AABB(Vector3(-area, -fall_height - 40.0f, -area), Vector3(area * 2.0f, fall_height + 50.0f, area * 2.0f)));
	splash->set_amount(MAX(64, max_drops / 4));
	splash->set_visibility_aabb(AABB(Vector3(-area, -fall_height - 40.0f, -area), Vector3(area * 2.0f, fall_height + 50.0f, area * 2.0f)));
	if (drops->is_inside_tree() && splash->is_inside_tree()) {
		drops->set_sub_emitter(splashes ? drops->get_path_to(splash) : NodePath());
	}
	splash->set_visible(splashes);
}

// ------------------------------------------------------------------ main thread

void FellowRain3D::_push_state() {
	bool enabled = is_visible_in_tree() && active == this && (wetness > 0.0f || rain > 0.0f) && Fellow::fluid_enabled();
	RendererRD::FellowWeather::State st;
	st.enabled = enabled;
	st.wetness = wetness;
	st.puddles = puddles;
	st.rain = rain;
	st.time = float(Math::fmod(time, 3600.0));
	if (occlusion && occlusion->is_inside_tree()) {
		st.occlusion_collider = occlusion->get_base();
		st.occlusion_transform = occlusion->get_global_transform();
		st.occlusion_extents = occlusion->get_size() * 0.5f;
	}
	st.ripple_tile = ripple_tile;
	st.ripple_strength = ripple_strength;
	PackedByteArray state;
	state.resize(sizeof(RendererRD::FellowWeather::State));
	memcpy(state.ptrw(), &st, sizeof(st));
	PushConstant pc;
	memset(&pc, 0, sizeof(pc));
	pc.size = ripple_size;
	pc.tile = ripple_tile;
	pc.time = st.time;
	pc.rain = rain;
	pc.amplitude = 0.0015f;
	PackedByteArray push;
	push.resize(sizeof(PushConstant));
	memcpy(push.ptrw(), &pc, sizeof(pc));
	RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowRain3D::_rt_update).bind(push, enabled, state));
}

void FellowRain3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			active = this;
			_build_children();
			set_process_internal(true);
		} break;
		case NOTIFICATION_READY: {
			_update_children();
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			double dt = get_process_delta_time();
			if (time_auto) {
				time += dt;
			}
			double step = last_time < 0.0 ? 0.0 : CLAMP(time - last_time, 0.0, 1.0);
			last_time = time;
			if (wetness_follows_rain) {
				if (rain > 0.0f) {
					wetness = MIN(1.0f, wetness + float(step) * rain / MAX(wetting_time, 0.01f));
				} else {
					wetness = MAX(0.0f, wetness - float(step) / MAX(drying_time, 0.01f));
				}
			}
			Camera3D *cam = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
			if (cam && drops) {
				Vector3 c = cam->get_global_position();
				// Upwind of the camera by the fall's drift, so the drops land around it.
				Vector3 at = c + Vector3(0, fall_height, 0) - wind * (fall_height / MAX(drop_speed, 0.1f));
				drops->set_global_position(at);
				splash->set_global_position(c);
			}
			if (occlusion) {
				Vector3 at = get_global_position();
				if (cam) {
					Vector3 c = cam->get_global_position();
					float step = occlusion_size / 16.0f;
					at.x = Math::floor(c.x / step + 0.5f) * step;
					at.z = Math::floor(c.z / step + 0.5f) * step;
				}
				if (!occlusion->get_global_transform().origin.is_equal_approx(at) || occlusion->get_global_basis() != Basis()) {
					occlusion->set_global_transform(Transform3D(Basis(), at));
				}
			}
			if (drops) {
				drops->set_amount_ratio(CLAMP(rain, 0.0f, 1.0f));
				drops->set_emitting(rain > 0.0f && is_visible_in_tree());
			}
			if (active == this) {
				_push_state();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (active == this) {
				active = nullptr;
				RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowRain3D::_rt_disable));
			}
			if (gpu_ready) {
				RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowRain3D::_rt_free));
			}
			last_time = -1.0;
		} break;
	}
}

// ------------------------------------------------------------------ render thread

void FellowRain3D::_rt_create() {
	rd = RS::get_singleton()->get_rendering_device();
	if (!rd) {
		gpu_failed = true;
		ERR_PRINT("FellowRain3D requires a RenderingDevice renderer (Forward+).");
		return;
	}
	if (shader_file.is_null()) {
		shader_file.instantiate();
		if (shader_file->parse_versions_from_text(fellow_rain_shader_glsl) != OK) {
			shader_file->print_errors("fellow_rain");
			gpu_failed = true;
			return;
		}
	}
	if (shader_ripples.is_null()) {
		shader_ripples = rd->shader_create_from_spirv(shader_file->get_spirv_stages("ripples"), "FellowRain ripples");
		shader_downsample = rd->shader_create_from_spirv(shader_file->get_spirv_stages("downsample"), "FellowRain downsample");
		if (shader_ripples.is_null() || shader_downsample.is_null()) {
			gpu_failed = true;
			return;
		}
		pipeline_ripples = rd->compute_pipeline_create(shader_ripples);
		pipeline_downsample = rd->compute_pipeline_create(shader_downsample);
	}
	int levels = int(Math::round(Math::log2(double(ripple_size)))) + 1;
	RD::TextureFormat tf;
	tf.width = ripple_size;
	tf.height = ripple_size;
	tf.mipmaps = levels;
	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
	ripple_tex = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(ripple_tex, "FellowRain ripples");
	ripple_mips.clear();
	for (int l = 0; l < levels; l++) {
		ripple_mips.push_back(rd->texture_create_shared_from_slice(RD::TextureView(), ripple_tex, 0, l));
	}
	Vector<RD::Uniform> u;
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, ripple_mips[0]));
	set_ripples = rd->uniform_set_create(u, shader_ripples, 0);
	sets_downsample.clear();
	for (int l = 0; l + 1 < levels; l++) {
		Vector<RD::Uniform> ud;
		ud.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, ripple_mips[l]));
		ud.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, ripple_mips[l + 1]));
		sets_downsample.push_back(rd->uniform_set_create(ud, shader_downsample, 0));
	}
	gpu_ready = true;
	ripple_texture->set_texture_rd_rid(ripple_tex);
}

void FellowRain3D::_rt_free() {
	if (!rd) {
		return;
	}
	if (RendererRD::FellowWeather *w = RendererRD::FellowWeather::get_singleton_if_exists()) {
		if (w->get_state().ripple_texture == ripple_tex) {
			w->set_state(RendererRD::FellowWeather::State());
		}
	}
	ripple_texture->set_texture_rd_rid(RID());
	for (RID &s : sets_downsample) {
		if (s.is_valid() && rd->uniform_set_is_valid(s)) {
			rd->free_rid(s);
		}
	}
	sets_downsample.clear();
	if (set_ripples.is_valid() && rd->uniform_set_is_valid(set_ripples)) {
		rd->free_rid(set_ripples);
	}
	set_ripples = RID();
	for (const RID &m : ripple_mips) {
		if (m.is_valid()) {
			rd->free_rid(m);
		}
	}
	ripple_mips.clear();
	if (ripple_tex.is_valid()) {
		rd->free_rid(ripple_tex);
		ripple_tex = RID();
	}
	gpu_ready = false;
}

void FellowRain3D::_rt_disable() {
	if (RendererRD::FellowWeather *w = RendererRD::FellowWeather::get_singleton_if_exists()) {
		w->set_state(RendererRD::FellowWeather::State());
	}
}

void FellowRain3D::_rt_update(PackedByteArray p_push, bool p_enabled, PackedByteArray p_state) {
	RendererRD::FellowWeather *w = RendererRD::FellowWeather::get_singleton();
	if (!w || gpu_failed || p_state.size() != int64_t(sizeof(RendererRD::FellowWeather::State)) || p_push.size() != int64_t(sizeof(PushConstant))) {
		return;
	}
	if (!gpu_ready) {
		_rt_create();
		if (!gpu_ready) {
			return;
		}
	}
	RendererRD::FellowWeather::State st;
	memcpy(&st, p_state.ptr(), sizeof(st));
	st.ripple_texture = ripple_tex;
	st.enabled = p_enabled;
	if (p_enabled) {
		PushConstant pc;
		memcpy(&pc, p_push.ptr(), sizeof(pc));
		int n = ripple_size;
		rd->draw_command_begin_label("FellowEngine rain ripples");
		RD::ComputeListID list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, pipeline_ripples);
		rd->compute_list_bind_uniform_set(list, set_ripples, 0);
		rd->compute_list_set_push_constant(list, &pc, sizeof(pc));
		rd->compute_list_dispatch(list, (n + 7) / 8, (n + 7) / 8, 1);
		rd->compute_list_end();
		for (int l = 0; l < sets_downsample.size(); l++) {
			pc.dst_size = MAX(n >> (l + 1), 1);
			list = rd->compute_list_begin();
			rd->compute_list_bind_compute_pipeline(list, pipeline_downsample);
			rd->compute_list_bind_uniform_set(list, sets_downsample[l], 0);
			rd->compute_list_set_push_constant(list, &pc, sizeof(pc));
			rd->compute_list_dispatch(list, (pc.dst_size + 7) / 8, (pc.dst_size + 7) / 8, 1);
			rd->compute_list_end();
		}
		rd->draw_command_end_label();
	}
	w->set_state(st);
}

// ------------------------------------------------------------------ settings

void FellowRain3D::set_rain(float p_rain) {
	rain = CLAMP(p_rain, 0.0f, 1.0f);
}

#define RAIN_CHILD_SETTER(m_func, m_type, m_member, m_expr) \
	void FellowRain3D::m_func(m_type p_value) { \
		m_member = m_expr; \
		_update_children(); \
	}

RAIN_CHILD_SETTER(set_wind, const Vector3 &, wind, p_value)
RAIN_CHILD_SETTER(set_area, float, area, MAX(1.0f, p_value))
RAIN_CHILD_SETTER(set_fall_height, float, fall_height, MAX(1.0f, p_value))
RAIN_CHILD_SETTER(set_max_drops, int, max_drops, CLAMP(p_value, 1, 1000000))
RAIN_CHILD_SETTER(set_drop_speed, float, drop_speed, MAX(0.1f, p_value))
RAIN_CHILD_SETTER(set_drop_size, const Vector2 &, drop_size, p_value)
RAIN_CHILD_SETTER(set_drop_color, const Color &, drop_color, p_value)
RAIN_CHILD_SETTER(set_splashes, bool, splashes, p_value)
RAIN_CHILD_SETTER(set_occlusion_size, float, occlusion_size, MAX(1.0f, p_value))
RAIN_CHILD_SETTER(set_occlusion_height, float, occlusion_height, MAX(1.0f, p_value))
RAIN_CHILD_SETTER(set_occlusion_resolution, int, occlusion_resolution, p_value)
RAIN_CHILD_SETTER(set_occlusion_always_update, bool, occlusion_always_update, p_value)

void FellowRain3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_rain", "rain"), &FellowRain3D::set_rain);
	ClassDB::bind_method(D_METHOD("get_rain"), &FellowRain3D::get_rain);
	ClassDB::bind_method(D_METHOD("set_wetness", "wetness"), &FellowRain3D::set_wetness);
	ClassDB::bind_method(D_METHOD("get_wetness"), &FellowRain3D::get_wetness);
	ClassDB::bind_method(D_METHOD("set_wetness_follows_rain", "enable"), &FellowRain3D::set_wetness_follows_rain);
	ClassDB::bind_method(D_METHOD("is_wetness_following_rain"), &FellowRain3D::is_wetness_following_rain);
	ClassDB::bind_method(D_METHOD("set_wetting_time", "seconds"), &FellowRain3D::set_wetting_time);
	ClassDB::bind_method(D_METHOD("get_wetting_time"), &FellowRain3D::get_wetting_time);
	ClassDB::bind_method(D_METHOD("set_drying_time", "seconds"), &FellowRain3D::set_drying_time);
	ClassDB::bind_method(D_METHOD("get_drying_time"), &FellowRain3D::get_drying_time);
	ClassDB::bind_method(D_METHOD("set_puddles", "puddles"), &FellowRain3D::set_puddles);
	ClassDB::bind_method(D_METHOD("get_puddles"), &FellowRain3D::get_puddles);
	ClassDB::bind_method(D_METHOD("set_wind", "wind"), &FellowRain3D::set_wind);
	ClassDB::bind_method(D_METHOD("get_wind"), &FellowRain3D::get_wind);
	ClassDB::bind_method(D_METHOD("set_area", "metres"), &FellowRain3D::set_area);
	ClassDB::bind_method(D_METHOD("get_area"), &FellowRain3D::get_area);
	ClassDB::bind_method(D_METHOD("set_fall_height", "metres"), &FellowRain3D::set_fall_height);
	ClassDB::bind_method(D_METHOD("get_fall_height"), &FellowRain3D::get_fall_height);
	ClassDB::bind_method(D_METHOD("set_max_drops", "count"), &FellowRain3D::set_max_drops);
	ClassDB::bind_method(D_METHOD("get_max_drops"), &FellowRain3D::get_max_drops);
	ClassDB::bind_method(D_METHOD("set_drop_speed", "speed"), &FellowRain3D::set_drop_speed);
	ClassDB::bind_method(D_METHOD("get_drop_speed"), &FellowRain3D::get_drop_speed);
	ClassDB::bind_method(D_METHOD("set_drop_size", "size"), &FellowRain3D::set_drop_size);
	ClassDB::bind_method(D_METHOD("get_drop_size"), &FellowRain3D::get_drop_size);
	ClassDB::bind_method(D_METHOD("set_drop_color", "color"), &FellowRain3D::set_drop_color);
	ClassDB::bind_method(D_METHOD("get_drop_color"), &FellowRain3D::get_drop_color);
	ClassDB::bind_method(D_METHOD("set_splashes", "enable"), &FellowRain3D::set_splashes);
	ClassDB::bind_method(D_METHOD("get_splashes"), &FellowRain3D::get_splashes);
	ClassDB::bind_method(D_METHOD("set_occlusion_size", "metres"), &FellowRain3D::set_occlusion_size);
	ClassDB::bind_method(D_METHOD("get_occlusion_size"), &FellowRain3D::get_occlusion_size);
	ClassDB::bind_method(D_METHOD("set_occlusion_height", "metres"), &FellowRain3D::set_occlusion_height);
	ClassDB::bind_method(D_METHOD("get_occlusion_height"), &FellowRain3D::get_occlusion_height);
	ClassDB::bind_method(D_METHOD("set_occlusion_resolution", "resolution"), &FellowRain3D::set_occlusion_resolution);
	ClassDB::bind_method(D_METHOD("get_occlusion_resolution"), &FellowRain3D::get_occlusion_resolution);
	ClassDB::bind_method(D_METHOD("set_occlusion_always_update", "enable"), &FellowRain3D::set_occlusion_always_update);
	ClassDB::bind_method(D_METHOD("get_occlusion_always_update"), &FellowRain3D::get_occlusion_always_update);
	ClassDB::bind_method(D_METHOD("set_ripple_tile", "metres"), &FellowRain3D::set_ripple_tile);
	ClassDB::bind_method(D_METHOD("get_ripple_tile"), &FellowRain3D::get_ripple_tile);
	ClassDB::bind_method(D_METHOD("set_ripple_strength", "strength"), &FellowRain3D::set_ripple_strength);
	ClassDB::bind_method(D_METHOD("get_ripple_strength"), &FellowRain3D::get_ripple_strength);
	ClassDB::bind_method(D_METHOD("set_time", "seconds"), &FellowRain3D::set_time);
	ClassDB::bind_method(D_METHOD("get_time"), &FellowRain3D::get_time);
	ClassDB::bind_method(D_METHOD("set_time_auto", "enabled"), &FellowRain3D::set_time_auto);
	ClassDB::bind_method(D_METHOD("is_time_auto"), &FellowRain3D::is_time_auto);
	ClassDB::bind_method(D_METHOD("get_ripple_texture"), &FellowRain3D::get_ripple_texture);
	ClassDB::bind_method(D_METHOD("get_drops"), &FellowRain3D::get_drops);
	ClassDB::bind_method(D_METHOD("get_occlusion"), &FellowRain3D::get_occlusion);

	ADD_GROUP("Rain", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rain", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_rain", "get_rain");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "wind", PROPERTY_HINT_NONE, "suffix:m/s"), "set_wind", "get_wind");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "area", PROPERTY_HINT_RANGE, "1,500,0.1,suffix:m"), "set_area", "get_area");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fall_height", PROPERTY_HINT_RANGE, "1,200,0.1,suffix:m"), "set_fall_height", "get_fall_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "max_drops", PROPERTY_HINT_RANGE, "1,1000000,1"), "set_max_drops", "get_max_drops");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drop_speed", PROPERTY_HINT_RANGE, "0.1,30,0.01,suffix:m/s"), "set_drop_speed", "get_drop_speed");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "drop_size", PROPERTY_HINT_NONE, "suffix:m"), "set_drop_size", "get_drop_size");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "drop_color"), "set_drop_color", "get_drop_color");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "splashes"), "set_splashes", "get_splashes");
	ADD_GROUP("Wet surfaces", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wetness", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_wetness", "get_wetness");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "wetness_follows_rain"), "set_wetness_follows_rain", "is_wetness_following_rain");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wetting_time", PROPERTY_HINT_RANGE, "0,3600,0.1,suffix:s"), "set_wetting_time", "get_wetting_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drying_time", PROPERTY_HINT_RANGE, "0,36000,0.1,suffix:s"), "set_drying_time", "get_drying_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "puddles", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_puddles", "get_puddles");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ripple_tile", PROPERTY_HINT_RANGE, "0.1,10,0.01,suffix:m"), "set_ripple_tile", "get_ripple_tile");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ripple_strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_ripple_strength", "get_ripple_strength");
	ADD_GROUP("Shelter", "occlusion_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_size", PROPERTY_HINT_RANGE, "1,1000,0.1,suffix:m"), "set_occlusion_size", "get_occlusion_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_height", PROPERTY_HINT_RANGE, "1,1000,0.1,suffix:m"), "set_occlusion_height", "get_occlusion_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "occlusion_resolution", PROPERTY_HINT_ENUM, "256:256,512:512,1024:1024,2048:2048,4096:4096"), "set_occlusion_resolution", "get_occlusion_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "occlusion_always_update"), "set_occlusion_always_update", "get_occlusion_always_update");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time", PROPERTY_HINT_NONE, "suffix:s", PROPERTY_USAGE_NONE), "set_time", "get_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "time_auto"), "set_time_auto", "is_time_auto");
}

FellowRain3D::FellowRain3D() {
	ripple_texture.instantiate();
}

FellowRain3D::~FellowRain3D() {
	if (active == this) {
		active = nullptr;
	}
	if (RS::get_singleton() && rd) {
		if (gpu_ready) {
			_rt_free();
		}
		if (shader_ripples.is_valid()) {
			rd->free_rid(shader_ripples);
		}
		if (shader_downsample.is_valid()) {
			rd->free_rid(shader_downsample);
		}
	}
}
