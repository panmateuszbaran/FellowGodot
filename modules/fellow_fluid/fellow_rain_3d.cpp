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

#include "fellow_editor_still.h"
#include "fellow_node_warnings.h"
#include "fellow_rain.glsl.gen.h"
#include "fellow_shared_shaders.h"
#include "fellow_snow_tracker_3d.h"
#include "fellow_snow_tracks.glsl.gen.h"
#include "fellow_wind_3d.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/primitive_meshes.h"
#include "scene/resources/particle_process_material.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/renderer_rd/effects/fellow_weather.h"
#include "servers/rendering/renderer_rd/storage_rd/particles_storage.h"
#include "servers/rendering/renderer_rd/storage_rd/texture_storage.h"
#include "servers/rendering/rendering_server.h"

FellowRain3D *FellowRain3D::active = nullptr;

// ------------------------------------------------------------------ children

// The drops and their splashes. A drop is a streak along its velocity (the particle's Y axis, wind included) turned to
// face the camera, soft across and thinning to both ends; lit (the sky's ambient and the scene's lights, brighter
// against a light behind it: water scatters forward), so a drop at night is dark but where a lamp shines through
// the rain. Within 0.5 m of the camera a drop is not drawn and it fades in to 4 m (one at the lens would be a blurred
// bar). A splash is a camera-facing droplet fading over its life. A snowflake (decision 0128) is a soft camera-facing
// disc swinging about its path at its own rate, fading in and out at the ends of its life (it lives in a box around
// the camera); its shader is opaque with hashed alpha and receives shadows (a flake in a building's shadow is not lit
// by the sun).
static const char *rain_drop_shader_code = R"(
shader_type spatial;
render_mode blend_mix, depth_draw_never, cull_disabled, shadows_disabled, specular_disabled, skip_vertex_transform;

uniform vec4 drop_color : source_color = vec4(0.85, 0.88, 0.92, 0.4);
uniform bool splash = false;
uniform bool flake = false;
uniform float flutter = 0.0;
uniform float fade_near = 0.5;
uniform float fade_far = 4.0;
// Thin streaks (decision 0141): the drops' own width (m; 0: the quad's) and the quad's width (m). A streak is drawn at
// its own width or a pixel, whichever is wider, its opacity scaled by their ratio (the light it carries the same): thin
// and faint in the distance instead of bars of the same width at every distance.
uniform float streak_width = 0.0;
uniform float quad_width = 0.024;
uniform float forward_scatter = 6.0;
uniform float light_wrap = 0.25;
// A drop refracts the sky and the lights around it into the view: brighter than a diffuse surface lit the same.
uniform float brightness = 2.5;

varying float v_alpha;

void vertex() {
	vec3 centre = MODEL_MATRIX[3].xyz;
	if (flake) {
		// Its own phase and rate (the golden ratio's sequence over the particles).
		float ph = fract(float(INSTANCE_ID) * 0.618034);
		float t = TIME * (1.4 + fract(ph * 7.31) * 1.4) + ph * TAU;
		centre += flutter * (0.6 + 0.8 * fract(ph * 3.17)) * vec3(sin(t), 0.25 * sin(t * 2.0), cos(t * 0.83 + 1.7));
	}
	vec3 to_cam = INV_VIEW_MATRIX[3].xyz - centre;
	float dist = length(to_cam);
	vec3 axis_x;
	vec3 axis_y;
	float sx = length(MODEL_MATRIX[0].xyz);
	float sy = length(MODEL_MATRIX[1].xyz);
	if (splash || flake) {
		axis_x = INV_VIEW_MATRIX[0].xyz;
		axis_y = INV_VIEW_MATRIX[1].xyz;
		float life = clamp(INSTANCE_CUSTOM.y, 0.0, 1.0);
		v_alpha = splash ? 1.0 - life : smoothstep(0.0, 0.08, life) * (1.0 - smoothstep(0.85, 1.0, life));
		sy = flake ? sx : sy;
	} else {
		axis_y = MODEL_MATRIX[1].xyz / max(sy, 1e-6);
		axis_x = cross(axis_y, to_cam / max(dist, 1e-6));
		float lx = length(axis_x);
		axis_x = lx > 1e-4 ? axis_x / lx : INV_VIEW_MATRIX[0].xyz;
		v_alpha = 1.0;
	}
	float fade = clamp((dist - fade_near) / (fade_far - fade_near), 0.0, 1.0);
	v_alpha *= fade;
	if (!splash && !flake && streak_width > 0.0) {
		float pixel = dist * 2.0 / (PROJECTION_MATRIX[1][1] * VIEWPORT_SIZE.y); // m per pixel at the drop
		float w = max(streak_width, pixel);
		v_alpha *= streak_width / w;
		sx *= w / max(quad_width, 1e-6);
	}
	vec3 wp = centre;
	if (fade > 0.0) {
		wp += axis_x * (VERTEX.x * sx) + axis_y * (VERTEX.y * sy);
	}
	VERTEX = (VIEW_MATRIX * vec4(wp, 1.0)).xyz;
	NORMAL = normalize((VIEW_MATRIX * vec4(to_cam, 0.0)).xyz);
}

void fragment() {
	vec2 p = UV * 2.0 - 1.0;
	float shape;
	if (splash) {
		shape = max(0.0, 1.0 - dot(p, p));
	} else if (flake) {
		shape = smoothstep(1.0, 0.25, length(p));
	} else {
		shape = (1.0 - p.x * p.x) * pow(max(sin(UV.y * PI), 0.0), 0.6);
	}
	ALBEDO = drop_color.rgb * brightness;
	ALPHA = drop_color.a * shape * v_alpha;
	// FLAKE_ALPHA_HASH
}

void light() {
	float behind = max(dot(-VIEW, LIGHT), 0.0);
	DIFFUSE_LIGHT += LIGHT_COLOR * ATTENUATION * (light_wrap + forward_scatter * pow(behind, 4.0)) / PI;
}
)";

static String _drop_code() {
	return String(rain_drop_shader_code);
}

// The snowflakes' (decision 0128): the same code, opaque with hashed alpha, receiving shadows.
static String _flake_code() {
	return String(rain_drop_shader_code).replace("render_mode blend_mix, depth_draw_never, cull_disabled, shadows_disabled,", "render_mode cull_disabled,").replace("// FLAKE_ALPHA_HASH", "ALPHA_HASH_SCALE = 1.0;");
}

// The process materials' flags, which make their code (their other properties are uniforms): the drops', the splashes'
// and the flakes' without and with the scene's wind. Shared by the node and the shader baker's provider.
static void _make_process_materials(Ref<ParticleProcessMaterial> &r_drop, Ref<ParticleProcessMaterial> &r_splash, Ref<ParticleProcessMaterial> r_flakes[2]) {
	r_drop.instantiate();
	r_drop->set_emission_shape(ParticleProcessMaterial::EMISSION_SHAPE_BOX);
	r_drop->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.8);
	r_drop->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.2);
	r_drop->set_spread(0.0);
	r_drop->set_gravity(Vector3());
	r_drop->set_particle_flag(ParticleProcessMaterial::PARTICLE_FLAG_ALIGN_Y_TO_VELOCITY, true);
	r_drop->set_collision_mode(ParticleProcessMaterial::COLLISION_HIDE_ON_CONTACT);
	// The flakes' own (decision 0156): before, the drops' process material changed its flags when it started to snow
	// (the scene's wind on, the splashes off) and compiled a new shader in that frame (12 ms on the RTX).
	for (int i = 0; i < 2; i++) {
		r_flakes[i] = r_drop->duplicate();
		r_flakes[i]->set_sub_emitter_mode(ParticleProcessMaterial::SUB_EMITTER_DISABLED);
		r_flakes[i]->set_wind_enabled(i == 1);
	}

	r_splash.instantiate();
	r_splash->set_direction(Vector3(0, 1, 0));
	r_splash->set_spread(40.0);
	r_splash->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 0.6);
	r_splash->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, 1.4);
	r_splash->set_param_min(ParticleProcessMaterial::PARAM_SCALE, 0.5);
	r_splash->set_param_max(ParticleProcessMaterial::PARAM_SCALE, 1.0);
	r_splash->set_gravity(Vector3(0, -9.8, 0));
	// A drop is stopped a step below the surface it hits (hide on contact does not move it out) and its splash is
	// emitted there: colliding, the splash is put back on the surface (a roof's splashes stay above its ceiling).
	r_splash->set_collision_mode(ParticleProcessMaterial::COLLISION_RIGID);
	r_splash->set_collision_bounce(0.1);
	r_splash->set_collision_friction(0.5);
}

static void _set_drop_splashes(const Ref<ParticleProcessMaterial> &p_drop, bool p_splashes) {
	p_drop->set_sub_emitter_mode(p_splashes ? ParticleProcessMaterial::SUB_EMITTER_AT_COLLISION : ParticleProcessMaterial::SUB_EMITTER_DISABLED);
	p_drop->set_sub_emitter_amount_at_collision(2);
}

void FellowRain3D::fellow_bake_shader_codes(Vector<String> &r_codes) {
	r_codes.push_back(_drop_code());
	r_codes.push_back(_flake_code());
	// The particles shaders of the process materials (milestone 61: written by ParticleProcessMaterial from its flags
	// when the node is set up, they were the 3 shaders a baked rain export still compiled at its first start).
	RenderingServer *rs = RenderingServer::get_singleton();
	if (!rs) {
		return;
	}
	Ref<ParticleProcessMaterial> drop, splash, flakes[2];
	_make_process_materials(drop, splash, flakes);
	Vector<Ref<ParticleProcessMaterial>> materials = { splash, flakes[0], flakes[1] };
	for (int s = 0; s < 2; s++) {
		Ref<ParticleProcessMaterial> d = drop->duplicate();
		_set_drop_splashes(d, s == 0);
		materials.push_back(d);
	}
	for (const Ref<ParticleProcessMaterial> &m : materials) {
		const String code = rs->shader_get_code(m->get_shader_rid());
		if (!code.is_empty() && !r_codes.has(code)) {
			r_codes.push_back(code);
		}
	}
}

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

	_make_process_materials(drop_process, splash_process, flake_process);

	drop_shader = FellowSharedShaders::shader(_drop_code()); // milestone 53: one per session
	drop_material.instantiate();
	drop_material->set_shader(drop_shader);
	drop_material->set_shader_parameter("splash", false);

	// The snowflakes' (decision 0128): the same code, opaque with hashed alpha (drawn in the opaque pass with the
	// particles' motion vectors: TAA reprojects them; blended, TAA wiped out flakes that move more than their size in
	// a frame), receiving shadows.
	flake_shader = FellowSharedShaders::shader(_flake_code());
	flake_material.instantiate();
	flake_material->set_shader(flake_shader);
	flake_material->set_shader_parameter("flake", true);
	flake_material->set_shader_parameter("fade_near", 0.3);
	flake_material->set_shader_parameter("fade_far", 1.2);
	flake_material->set_shader_parameter("forward_scatter", 4.0); // ice crystals scatter forward: flakes glow against the sun
	flake_material->set_shader_parameter("light_wrap", 0.5);
	flake_material->set_shader_parameter("brightness", 1.0);

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

	// The flakes' material on a hidden quad, so that its pipelines compile when the node is set up, not in the frame it
	// starts to snow (decision 0156: 6 surface pipelines, 51 ms on the RTX, when Slice Lab's weather first turned to
	// snow). The drops' need none: the splashes wear their shader from the start.
	Ref<QuadMesh> flake_pipelines_mesh;
	flake_pipelines_mesh.instantiate();
	flake_pipelines_mesh->set_material(flake_material);
	MeshInstance3D *flake_pipelines = memnew(MeshInstance3D);
	flake_pipelines->set_name("SnowFlakesPipelines");
	flake_pipelines->set_mesh(flake_pipelines_mesh);
	flake_pipelines->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	flake_pipelines->set_visible(false);
	add_child(flake_pipelines, false, INTERNAL_MODE_FRONT);

	splash_material.instantiate();
	splash_material->set_shader(drop_shader);
	splash_material->set_shader_parameter("splash", true);

	Ref<QuadMesh> splash_mesh;
	splash_mesh.instantiate();
	splash_mesh->set_size(Vector2(0.014, 0.014));
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

	// The process materials run once (one particle each, no draw pass, always in view) while the node is set up, so
	// that their shaders compile then, not when it first rains or snows (decision 0156: a particles shader is compiled
	// when particles first use it; 12 ms on the RTX when Slice Lab's weather first turned to snow).
	const Ref<ParticleProcessMaterial> warm[4] = { drop_process, splash_process, flake_process[0], flake_process[1] };
	for (int i = 0; i < 4; i++) {
		GPUParticles3D *w = memnew(GPUParticles3D);
		w->set_name(vformat("ProcessWarmer%d", i));
		w->set_as_top_level(true);
		w->set_amount(1);
		w->set_lifetime(0.05);
		w->set_one_shot(true);
		w->set_process_material(warm[i]);
		w->set_draw_pass_mesh(0, Ref<Mesh>());
		w->set_visibility_aabb(AABB(Vector3(-1e6, -1e6, -1e6), Vector3(2e6, 2e6, 2e6)));
		w->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		add_child(w, false, INTERNAL_MODE_FRONT);
		w->set_emitting(true);
		process_warmers.push_back(w);
	}
	process_warm_frames = 0;
	_update_children();
}

void FellowRain3D::_free_process_warmers() {
	for (GPUParticles3D *w : process_warmers) {
		remove_child(w);
		memdelete(w);
	}
	process_warmers.clear();
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

	const bool flakes = _is_snow();
	const bool splashing = splashes && !flakes;
	const float fall_speed = flakes ? flake_speed : drop_speed;
	Vector3 v = wind_applied - Vector3(0, fall_speed, 0);
	float speed = v.length();
	const bool flake_wind = flakes && wind_from_scene && FellowWind3D::get_active() != nullptr;
	const Ref<ParticleProcessMaterial> pm = flakes ? flake_process[flake_wind ? 1 : 0] : drop_process;
	pm->set_direction(v / MAX(speed, 0.01f));
	pm->set_param_min(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, speed * 0.95);
	pm->set_param_max(ParticleProcessMaterial::PARAM_INITIAL_LINEAR_VELOCITY, speed * 1.05);
	pm->set_param_min(ParticleProcessMaterial::PARAM_SCALE, flakes ? 0.5 : 0.8);
	pm->set_param_max(ParticleProcessMaterial::PARAM_SCALE, flakes ? 1.5 : 1.2);
	// The flakes live in a box around the camera (from 3 m below it to fall_height above), and take the scene's wind
	// where they are, gusts included (decision 0127).
	float box_height = fall_height + 6.0f;
	pm->set_emission_box_extents(Vector3(area * 0.5f, flakes ? box_height * 0.5f : 0.5f, area * 0.5f));
	pm->set_wind_influence(flake_wind_influence);
	// The drops' process material is the rain's alone: its splashes whatever the precipitation now (its code, and so its
	// shader, the same when it snows; decision 0156).
	_set_drop_splashes(drop_process, splashes);
	if (drops->get_process_material() != pm) {
		drops->set_process_material(pm);
	}
	drop_material->set_shader_parameter("drop_color", drop_color);
	drop_material->set_shader_parameter("streak_width", streak_width);
	drop_material->set_shader_parameter("quad_width", drop_size.x);
	splash_material->set_shader_parameter("drop_color", drop_color);
	flake_material->set_shader_parameter("drop_color", flake_color);
	flake_material->set_shader_parameter("flutter", flake_flutter);
	Ref<QuadMesh> qm = drops->get_draw_pass_mesh(0);
	if (qm.is_valid()) {
		qm->set_size(flakes ? Vector2(flake_size, flake_size) : drop_size);
		Ref<Material> m = flakes ? Ref<Material>(flake_material) : Ref<Material>(drop_material);
		if (qm->get_material() != m) {
			qm->set_material(m);
		}
	}
	// The drops fall from fall_height above the camera to well below it (the ground may be far below).
	double lifetime = flakes ? double(SNOW_LIFETIME) : (fall_height + 30.0) / MAX(drop_speed, 0.1f);
	if (drops->get_amount() != max_drops) {
		drops->set_amount(max_drops);
	}
	if (!Math::is_equal_approx(drops->get_lifetime(), lifetime)) {
		drops->set_lifetime(lifetime);
		drops->set_pre_process_time(lifetime);
	}
	if (flakes) {
		// The box, and how far the flakes drift and fall in a life (twice the wind at the camera: the gusts).
		float drift = wind_applied.length() * SNOW_LIFETIME * 2.0f + 2.0f;
		float drop = flake_speed * SNOW_LIFETIME + 2.0f;
		drops->set_visibility_aabb(AABB(Vector3(-area * 0.5f - drift, -box_height * 0.5f - drop, -area * 0.5f - drift), Vector3(area + drift * 2.0f, box_height + drop + 2.0f, area + drift * 2.0f)));
	} else {
		drops->set_visibility_aabb(AABB(Vector3(-area, -fall_height - 40.0f, -area), Vector3(area * 2.0f, fall_height + 50.0f, area * 2.0f)));
	}
	splash->set_amount(MAX(64, max_drops / 4));
	splash->set_visibility_aabb(AABB(Vector3(-area, -fall_height - 40.0f, -area), Vector3(area * 2.0f, fall_height + 50.0f, area * 2.0f)));
	if (drops->is_inside_tree() && splash->is_inside_tree()) {
		drops->set_sub_emitter(splashing ? drops->get_path_to(splash) : NodePath());
	}
	splash->set_visible(splashing);
}

// ------------------------------------------------------------------ main thread

void FellowRain3D::_push_state() {
	bool enabled = is_visible_in_tree() && active == this && (wetness > 0.0f || rain > 0.0f || snow > 0.0f || frost > 0.0f) && Fellow::fluid_enabled();
	// Built in zeroed memory, its padding the same every time: the bytes are compared with those last sent.
	alignas(RendererRD::FellowWeather::State) uint8_t st_bytes[sizeof(RendererRD::FellowWeather::State)] = {};
	RendererRD::FellowWeather::State &st = *::new (st_bytes) RendererRD::FellowWeather::State;
	st.enabled = enabled;
	st.wetness = wetness;
	st.puddles = puddles;
	st.rain = get_ripple_rain();
	st.snow = snow;
	st.snow_sparkle = snow_sparkle;
	st.freeze = freeze;
	st.frost = frost;
	st.drying_patches = drying_patches;
	st.slush = slush;
	// Wind-driven rain (decision 0139): the drops' drift per metre of fall, the wind they were given over their speed;
	// from a drift of 0.2 (a breeze of 1.8 m/s at the drops' 9 m/s: the rain as good as vertical) to fully slanted at
	// 0.6 (5.4 m/s). Not for snow.
	if (!_is_snow() && wind_driven > 0.0f) {
		const Vector2 drift = Vector2(wind_applied.x, wind_applied.z) / MAX(drop_speed, 0.1f);
		const float slant = CLAMP((drift.length() - 0.2f) * 2.5f, 0.0f, 1.0f) * wind_driven;
		if (slant > 0.0f) {
			st.rain_drift = drift;
			st.rain_slant = slant;
		}
	}
	st.showers = (is_visible_in_tree() && active == this && rain > 0.0f) ? showers : 0.0f;
	st.rainbow = is_visible_in_tree() && active == this ? rainbow * rain_seen : 0.0f;
	st.rainbow_enabled = is_visible_in_tree() && active == this && rainbow > 0.0f;
	st.shafts = is_visible_in_tree() && active == this ? MIN(shafts * rain, 1.0f) : 0.0f;
	st.shafts_snow = _is_snow();
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
	pc.levels = int(Math::round(Math::log2(double(ripple_size)))) + 1;
	pc.tile = ripple_tile;
	pc.time = st.time;
	pc.rain = get_ripple_rain();
	pc.amplitude = 0.0015f;
	PackedByteArray push;
	push.resize(sizeof(PushConstant));
	memcpy(push.ptrw(), &pc, sizeof(pc));

	// The snow's tracks (decision 0130): the window around the camera (in whole cells), this frame's stamps (the
	// trackers' and those asked for), and how much the falling snow has filled them back since the last pass that
	// did it (a tenth of a millimetre at a time: smaller steps would be lost in the texture's half floats).
	PackedByteArray tracks_data;
	if (enabled && _tracks_active()) {
		TracksPush tp;
		memset(&tp, 0, sizeof(tp));
		int size = track_resolution;
		float cell = track_window / float(size);
		Camera3D *cam = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
		Vector3 c = cam ? cam->get_global_position() : get_global_position();
		tp.origin[0] = int32_t(Math::floor(c.x / cell + 0.5f));
		tp.origin[1] = int32_t(Math::floor(c.z / cell + 0.5f));
		tp.size = size;
		tp.cell = cell;
		if (_is_snow() && rain > 0.0f && track_refill_time > 0.0f && !fellow_editor_still()) {
			track_refill_accum += double(rain * rain_here) * snow_depth / track_refill_time * MIN(get_process_delta_time(), 1.0);
		}
		if (track_refill_accum >= 1e-4) {
			tp.refill = float(track_refill_accum);
			track_refill_accum = 0.0;
		}
		// The trackers first (every frame), then the stamps asked for (the rest wait for the next frames).
		LocalVector<TrackStamp> frame_stamps;
		for (FellowSnowTracker3D *t : FellowSnowTracker3D::get_trackers()) {
			TrackStamp s;
			if (frame_stamps.size() < uint32_t(MAX_TRACK_STAMPS) && t->take_stamp(s.segment, s.shape, snow_depth)) {
				frame_stamps.push_back(s);
			}
		}
		uint32_t taken = 0;
		while (frame_stamps.size() < uint32_t(MAX_TRACK_STAMPS) && taken < pending_stamps.size()) {
			frame_stamps.push_back(pending_stamps[taken++]);
		}
		uint32_t n = frame_stamps.size();
		tp.stamp_count = int32_t(n);
		tracks_data.resize(sizeof(TracksPush) + n * sizeof(TrackStamp));
		memcpy(tracks_data.ptrw(), &tp, sizeof(tp));
		if (n > 0) {
			memcpy(tracks_data.ptrw() + sizeof(TracksPush), frame_stamps.ptr(), n * sizeof(TrackStamp));
		}
		if (taken < pending_stamps.size()) {
			LocalVector<TrackStamp> rest;
			for (uint32_t i = taken; i < pending_stamps.size(); i++) {
				rest.push_back(pending_stamps[i]);
			}
			pending_stamps = rest;
		} else {
			pending_stamps.clear();
		}
		st.snow_depth = snow_depth;
		st.tracks_origin = Vector2(tp.origin[0] * cell, tp.origin[1] * cell);
		st.tracks_window = track_window;
		memcpy(state.ptrw(), &st, sizeof(st));
	}
	// Milestone 52: the ripples' pass only while it rains, and once more when it stops (they flatten): a wet scene
	// without rain dispatched it every frame, for the same flat ripples (in the editor, between the frames it drew).
	const bool ripples = enabled && (pc.rain > 0.0f || sent_ripple_rain > 0.0f);
	sent_ripple_rain = enabled ? pc.rain : 0.0f;
	// The tracks' pass with nothing to do: no stamp, no refill, the window where it was.
	bool tracks_idle = tracks_data.is_empty();
	if (!tracks_idle) {
		TracksPush tp;
		memcpy(&tp, tracks_data.ptr(), sizeof(tp));
		tracks_idle = tp.stamp_count == 0 && tp.refill <= 0.0f && tp.origin[0] == sent_tracks_origin[0] && tp.origin[1] == sent_tracks_origin[1];
		sent_tracks_origin[0] = tp.origin[0];
		sent_tracks_origin[1] = tp.origin[1];
	}
	// Still in the editor (milestone 52): nothing sent when nothing changed.
	if (fellow_editor_still() && tracks_idle && sent_enabled == int(enabled) && push == sent_push && state == sent_state) {
		return;
	}
	sent_enabled = int(enabled);
	sent_push = push;
	sent_state = state;
	RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowRain3D::_rt_update).bind(push, enabled, ripples, state, tracks_data));
}

bool FellowRain3D::_tracks_active() const {
	return tracks && snow > 0.0f;
}

void FellowRain3D::stamp(const Vector3 &p_from, const Vector3 &p_to, float p_radius, float p_depth) {
	TrackStamp s;
	s.segment[0] = p_from.x;
	s.segment[1] = p_from.z;
	s.segment[2] = p_to.x;
	s.segment[3] = p_to.z;
	s.shape[0] = MAX(0.005f, p_radius);
	s.shape[1] = p_depth >= 0.0f ? p_depth : snow_depth * 0.8f;
	s.shape[2] = 0.0f;
	s.shape[3] = -1.0f; // no contact test
	add_track_stamp(s);
}

void FellowRain3D::add_track_stamp(const TrackStamp &p_stamp) {
	if (pending_stamps.size() < uint32_t(MAX_TRACK_STAMPS) * 64) {
		pending_stamps.push_back(p_stamp);
	}
}

void FellowRain3D::set_tracks(bool p_enable) {
	tracks = p_enable;
	for (FellowSnowTracker3D *t : FellowSnowTracker3D::get_trackers()) {
		t->update_configuration_warnings();
	}
}

void FellowRain3D::set_track_resolution(int p_resolution) {
	// A power of two (the pass wraps the texels with a mask).
	int r = 256;
	while (r < p_resolution && r < 8192) {
		r *= 2;
	}
	track_resolution = r;
}

PackedStringArray FellowRain3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (is_inside_tree() && active != this) {
		warnings.push_back(RTR("Another FellowRain3D is the rain: one rain node works at a time (the last to enter the tree)."));
	}
	if (!Fellow::fluid_enabled()) {
		warnings.push_back(RTR("FellowEngine's fluids are off in the Project Settings (Rendering > Fellow > Enabled, or Rendering > Fellow > Fluid > Enabled): only the falling drops are drawn."));
	} else if (FellowWarnings::mobile() || FellowWarnings::compatibility()) {
		warnings.push_back(RTR("Wet, snowy and frosted surfaces, puddles, ripples and shelter are drawn only with the Forward+ renderer: this one draws the falling drops alone."));
	}
	return warnings;
}

void FellowRain3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_ENTER_TREE: {
			FellowRain3D *previous = active;
			active = this;
			if (previous && previous != this) {
				previous->update_configuration_warnings(); // no longer the rain
			}
			for (FellowSnowTracker3D *t : FellowSnowTracker3D::get_trackers()) {
				t->update_configuration_warnings();
			}
			_build_children();
			set_process_internal(true);
		} break;
		case NOTIFICATION_READY: {
			_update_children();
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!process_warmers.is_empty() && ++process_warm_frames > 8) {
				_free_process_warmers();
			}
			double dt = get_process_delta_time();
			// Still in the editor with its Animate Nodes toggle off (milestone 52): no time, no falling drops. Nothing
			// written that did not change (each write had the renderer redraw).
			const bool still = fellow_editor_still();
			if (time_auto && !still) {
				time += dt;
			}
			double step = last_time < 0.0 ? 0.0 : CLAMP(time - last_time, 0.0, 1.0);
			last_time = time;
			// Showers (decision 0137): the rain at the camera as the clouds above let it fall (read back from the
			// clouds' layer); without clouds, or unknown yet, as it is set.
			{
				float target = 1.0f;
				const float probed = (showers > 0.0f && rain > 0.0f) ? RendererRD::FellowWeather::get_rain_here() : -1.0f;
				if (probed >= 0.0f) {
					target = Math::lerp(1.0f, probed, showers);
					if (!rain_here_seen) {
						rain_here = target; // the first reading: no rain easing out at the start
						rain_here_seen = true;
					}
				} else if (showers > 0.0f && rain > 0.0f && !rain_here_seen) {
					target = rain_here;
				}
				rain_here += (target - rain_here) * float(1.0 - Math::exp(-step / double(SHOWER_TIME)));
			}
			const float falling = rain * rain_here;
			if (wetness_follows_rain) {
				if (get_ripple_rain() > 0.0f) {
					wetness = MIN(1.0f, wetness + float(step) * falling / MAX(wetting_time, 0.01f));
				} else {
					wetness = MAX(0.0f, wetness - float(step) / MAX(drying_time, 0.01f));
				}
			}
			// The rainbow (decision 0134): the rain seen lately, fading once it stops (the rain moving away); none once it
			// snows.
			if (_is_snow() && rain > 0.0f) {
				rain_seen = 0.0f;
			} else {
				// The rain around, not only here (under a gap in the showers the bow stands in them).
				rain_seen = MAX(precipitation == PRECIPITATION_RAIN ? rain : 0.0f, rain_seen * float(Math::exp(-step / double(RAINBOW_TIME))));
			}
			if (snow_follows_precipitation) {
				if (_is_snow() && rain > 0.0f) {
					snow = MIN(1.0f, snow + float(step) * falling / MAX(snowing_time, 0.01f));
				} else {
					snow = MAX(0.0f, snow - float(step) / MAX(melting_time, 0.01f));
				}
			}
			Camera3D *cam = get_viewport() ? get_viewport()->get_camera_3d() : nullptr;
			if (cam && drops) {
				Vector3 c = cam->get_global_position();
				// Upwind of the camera by the fall's drift, so the drops land around it.
				// The scene's wind at the camera (its gust there), if it has one (decision 0125).
				Vector3 w = wind;
				if (wind_from_scene && FellowWind3D::get_active()) {
					w = FellowWind3D::get_active()->get_velocity_at(c);
				}
				bool flake_wind = _is_snow() && wind_from_scene && FellowWind3D::get_active() != nullptr;
				const Ref<Material> want = _is_snow() ? flake_process[flake_wind ? 1 : 0] : drop_process;
				if ((!w.is_equal_approx(wind_applied) && w.distance_to(wind_applied) > 0.25f) || drops->get_process_material() != want) {
					wind_applied = w;
					_update_children();
				}
				Vector3 at;
				if (_is_snow()) {
					// The flakes' box, raised and upwind by half what they fall and drift in a life.
					at = c + Vector3(0, fall_height * 0.5f - 3.0f + flake_speed * SNOW_LIFETIME * 0.5f, 0) - wind_applied * (SNOW_LIFETIME * 0.5f);
				} else {
					at = c + Vector3(0, fall_height, 0) - wind_applied * (fall_height / MAX(drop_speed, 0.1f));
				}
				if (!drops->get_global_position().is_equal_approx(at)) {
					drops->set_global_position(at);
				}
				if (!splash->get_global_position().is_equal_approx(c)) {
					splash->set_global_position(c);
				}
			}
			if (occlusion) {
				Vector3 at = get_global_position();
				if (cam) {
					Vector3 c = cam->get_global_position();
					float snap = occlusion_size / 16.0f;
					at.x = Math::floor(c.x / snap + 0.5f) * snap;
					at.z = Math::floor(c.z / snap + 0.5f) * snap;
				}
				if (!occlusion->get_global_transform().origin.is_equal_approx(at) || occlusion->get_global_basis() != Basis()) {
					occlusion->set_global_transform(Transform3D(Basis(), at));
				}
			}
			if (drops) {
				const float ratio = CLAMP(falling, 0.0f, 1.0f);
				if (drops->get_amount_ratio() != ratio) {
					drops->set_amount_ratio(ratio);
				}
				const bool emit = falling > 0.0f && is_visible_in_tree() && !still;
				if (drops->is_emitting() != emit) {
					drops->set_emitting(emit);
				}
			}
			if (active == this) {
				_push_state();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (active == this) {
				active = nullptr;
				RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowRain3D::_rt_disable));
				for (FellowSnowTracker3D *t : FellowSnowTracker3D::get_trackers()) {
					t->update_configuration_warnings();
				}
			}
			if (gpu_ready) {
				RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowRain3D::_rt_free));
			}
			last_time = -1.0;
			sent_enabled = -1;
			sent_ripple_rain = 1.0f;
			sent_tracks_origin[0] = INT32_MAX;
		} break;
	}
}

// ------------------------------------------------------------------ render thread

void FellowRain3D::_rt_create() {
	rd = RS::get_singleton()->get_rendering_device();
	if (!rd) {
		gpu_failed = true;
		WARN_PRINT("FellowRain3D needs a RenderingDevice renderer (Forward+ or Mobile) for its ripples and puddles: with the Compatibility renderer only the drops fall.");
		return;
	}
	if (shader_file.is_null()) {
		shader_file = FellowSharedShaders::file(fellow_rain_shader_glsl, "fellow_rain"); // milestone 53
		if (shader_file.is_null()) {
			gpu_failed = true;
			return;
		}
	}
	// The snow tracks' kernel and pipeline made now too (milestone 68): made at the first footprint in the snow, the
	// kernel was compiled from GLSL in play (77 ms in Frontier Lab's soak with the kernel cache cold).
	_rt_create_tracks_pipeline();
	if (shader_ripples.is_null()) {
		shader_ripples = rd->shader_create_from_spirv(shader_file->get_spirv_stages("ripples"), "FellowRain ripples");
		shader_tail = rd->shader_create_from_spirv(shader_file->get_spirv_stages("tail"), "FellowRain mip tail");
		if (shader_ripples.is_null() || shader_tail.is_null()) {
			gpu_failed = true;
			return;
		}
		pipeline_ripples = rd->compute_pipeline_create(shader_ripples);
		pipeline_tail = rd->compute_pipeline_create(shader_tail);
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
	// Levels 0-4 written by the tiles, 4 read and 5-8 written by the tail (a level that does not exist: the last one,
	// never written).
	Vector<RD::Uniform> u;
	for (int b = 0; b < 5; b++) {
		u.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, b, ripple_mips[MIN(b, levels - 1)]));
	}
	set_ripples = rd->uniform_set_create(u, shader_ripples, 0);
	Vector<RD::Uniform> ut;
	ut.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, ripple_mips[MIN(4, levels - 1)]));
	for (int b = 1; b < 5; b++) {
		ut.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, b, ripple_mips[MIN(4 + b, levels - 1)]));
	}
	set_tail = rd->uniform_set_create(ut, shader_tail, 0);
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
	_rt_free_tracks();
	if (set_tail.is_valid() && rd->uniform_set_is_valid(set_tail)) {
		rd->free_rid(set_tail);
	}
	set_tail = RID();
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

void FellowRain3D::_rt_update(PackedByteArray p_push, bool p_enabled, bool p_ripples, PackedByteArray p_state, PackedByteArray p_tracks) {
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
	w->fellow_note_wetting_node(); // decision 0161: materials keep the wetting code from now on
	if (p_ripples) {
		PushConstant pc;
		memcpy(&pc, p_push.ptr(), sizeof(pc));
		int n = ripple_size;
		rd->draw_command_begin_label("FellowEngine rain ripples");
		RD::ComputeListID list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, pipeline_ripples);
		rd->compute_list_bind_uniform_set(list, set_ripples, 0);
		rd->compute_list_set_push_constant(list, &pc, sizeof(pc));
		rd->compute_list_dispatch(list, n / 16, n / 16, 1);
		rd->compute_list_add_barrier(list);
		rd->compute_list_bind_compute_pipeline(list, pipeline_tail);
		rd->compute_list_bind_uniform_set(list, set_tail, 0);
		rd->compute_list_set_push_constant(list, &pc, sizeof(pc));
		rd->compute_list_dispatch(list, 1, 1, 1);
		rd->compute_list_end();
		rd->draw_command_end_label();
	}
	if (p_enabled && p_tracks.size() >= int64_t(sizeof(TracksPush))) {
		_rt_tracks(st, p_tracks);
	} else if (tracks_tex.is_valid()) {
		_rt_free_tracks();
	}
	w->set_state(st);
}

bool FellowRain3D::_rt_create_tracks_pipeline() {
	if (tracks_shader.is_valid()) {
		return true;
	}
	if (tracks_shader_file.is_null()) {
		tracks_shader_file = FellowSharedShaders::file(fellow_snow_tracks_shader_glsl, "fellow_snow_tracks"); // milestone 53
	}
	if (tracks_shader_file.is_null()) {
		return false;
	}
	tracks_shader = rd->shader_create_from_spirv(tracks_shader_file->get_spirv_stages(), "FellowRain snow tracks");
	if (tracks_shader.is_null()) {
		return false;
	}
	tracks_pipeline = rd->compute_pipeline_create(tracks_shader);
	RD::SamplerState ss;
	ss.mag_filter = RD::SAMPLER_FILTER_NEAREST;
	ss.min_filter = RD::SAMPLER_FILTER_NEAREST;
	ss.repeat_u = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	ss.repeat_v = RD::SAMPLER_REPEAT_MODE_CLAMP_TO_EDGE;
	tracks_sampler = rd->sampler_create(ss);
	tracks_stamps = rd->storage_buffer_create(MAX_TRACK_STAMPS * sizeof(TrackStamp));
	return true;
}

void FellowRain3D::_rt_tracks(RendererRD::FellowWeather::State &r_state, const PackedByteArray &p_tracks) {
	TracksPush tp;
	memcpy(&tp, p_tracks.ptr(), sizeof(tp));
	int n = tp.stamp_count;
	if (p_tracks.size() != int64_t(sizeof(TracksPush) + n * sizeof(TrackStamp))) {
		return;
	}
	if (!_rt_create_tracks_pipeline()) {
		return;
	}
	if (tracks_tex.is_null() || tracks_size != tp.size) {
		_rt_free_tracks();
		RD::TextureFormat tf;
		tf.width = tp.size;
		tf.height = tp.size;
		tf.format = RD::DATA_FORMAT_R16_SFLOAT;
		tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
		tracks_tex = rd->texture_create(tf, RD::TextureView());
		rd->set_resource_name(tracks_tex, "FellowRain snow tracks");
		rd->texture_clear(tracks_tex, Color(0, 0, 0, 0), 0, 1, 0, 1);
		tracks_size = tp.size;
		tracks_cell = tp.cell;
		tracks_prev_origin[0] = tp.origin[0];
		tracks_prev_origin[1] = tp.origin[1];
	} else if (tracks_cell != tp.cell) {
		// The window's size changed: its cells are other places.
		rd->texture_clear(tracks_tex, Color(0, 0, 0, 0), 0, 1, 0, 1);
		tracks_cell = tp.cell;
		tracks_prev_origin[0] = tp.origin[0];
		tracks_prev_origin[1] = tp.origin[1];
	}
	// The shelter heightfield, for the trackers' contact test.
	RID occ;
	if (r_state.occlusion_collider.is_valid()) {
		RendererRD::ParticlesStorage *ps = RendererRD::ParticlesStorage::get_singleton();
		if (ps && ps->particles_collision_is_heightfield(r_state.occlusion_collider)) {
			occ = ps->fellow_particles_collision_get_heightfield_texture(r_state.occlusion_collider);
			if (!rd->texture_is_valid(occ)) {
				occ = RID();
			}
		}
	}
	if (occ.is_valid() && r_state.occlusion_extents.x > 0.0f && r_state.occlusion_extents.z > 0.0f) {
		tp.occ_origin[0] = r_state.occlusion_transform.origin.x;
		tp.occ_origin[1] = r_state.occlusion_transform.origin.z;
		tp.occ_inv[0] = 1.0f / (2.0f * r_state.occlusion_extents.x);
		tp.occ_inv[1] = 1.0f / (2.0f * r_state.occlusion_extents.z);
		tp.occ_y = r_state.occlusion_transform.origin.y;
		tp.occ_half = r_state.occlusion_extents.y;
		tp.occ_enabled = 1.0f;
	}
	RID occ_bound = occ.is_valid() ? occ : RendererRD::TextureStorage::get_singleton()->texture_rd_get_default(RendererRD::TextureStorage::DEFAULT_RD_TEXTURE_BLACK);
	if (tracks_set.is_null() || !rd->uniform_set_is_valid(tracks_set) || tracks_set_occlusion != occ_bound) {
		if (tracks_set.is_valid() && rd->uniform_set_is_valid(tracks_set)) {
			rd->free_rid(tracks_set);
		}
		Vector<RD::Uniform> u;
		u.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, tracks_tex));
		u.push_back(RD::Uniform(RD::UNIFORM_TYPE_TEXTURE, 1, occ_bound));
		u.push_back(RD::Uniform(RD::UNIFORM_TYPE_SAMPLER, 2, tracks_sampler));
		u.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, tracks_stamps));
		tracks_set = rd->uniform_set_create(u, tracks_shader, 0);
		tracks_set_occlusion = occ_bound;
	}
	if (n > 0) {
		rd->buffer_update(tracks_stamps, 0, n * sizeof(TrackStamp), p_tracks.ptr() + sizeof(TracksPush));
	}
	tp.prev_origin[0] = tracks_prev_origin[0];
	tp.prev_origin[1] = tracks_prev_origin[1];
	tracks_prev_origin[0] = tp.origin[0];
	tracks_prev_origin[1] = tp.origin[1];

	// The rectangles of cells to update (in the window): all of it when the falling snow fills the tracks back or the
	// window jumped; otherwise the strips it entered (cleared), then the stamps' bounds.
	struct Rect {
		int32_t lo[2];
		int32_t size[2];
		bool clear = false;
		Rect() {
			lo[0] = lo[1] = size[0] = size[1] = 0;
		}
		Rect(int32_t p_x, int32_t p_z, int32_t p_w, int32_t p_h, bool p_clear) {
			lo[0] = p_x;
			lo[1] = p_z;
			size[0] = p_w;
			size[1] = p_h;
			clear = p_clear;
		}
	};
	LocalVector<Rect> rects;
	const int32_t half = tp.size / 2;
	const int32_t win_lo[2] = { tp.origin[0] - half, tp.origin[1] - half };
	const int32_t d[2] = { tp.origin[0] - tp.prev_origin[0], tp.origin[1] - tp.prev_origin[1] };
	if (tp.refill > 0.0f || Math::abs(d[0]) >= tp.size || Math::abs(d[1]) >= tp.size) {
		rects.push_back(Rect(win_lo[0], win_lo[1], tp.size, tp.size, true));
	} else {
		if (d[0] != 0) {
			// The columns entered along x, the window's whole depth.
			int32_t x0 = d[0] > 0 ? win_lo[0] + tp.size - d[0] : win_lo[0];
			rects.push_back(Rect(x0, win_lo[1], Math::abs(d[0]), tp.size, true));
		}
		if (d[1] != 0) {
			int32_t z0 = d[1] > 0 ? win_lo[1] + tp.size - d[1] : win_lo[1];
			rects.push_back(Rect(win_lo[0], z0, tp.size, Math::abs(d[1]), true));
		}
		if (n > 0) {
			const TrackStamp *sp = reinterpret_cast<const TrackStamp *>(p_tracks.ptr() + sizeof(TracksPush));
			float lo[2] = { 1e30f, 1e30f };
			float hi[2] = { -1e30f, -1e30f };
			for (int i = 0; i < n; i++) {
				float r = sp[i].shape[0] * 1.5f + tp.cell;
				lo[0] = MIN(lo[0], MIN(sp[i].segment[0], sp[i].segment[2]) - r);
				lo[1] = MIN(lo[1], MIN(sp[i].segment[1], sp[i].segment[3]) - r);
				hi[0] = MAX(hi[0], MAX(sp[i].segment[0], sp[i].segment[2]) + r);
				hi[1] = MAX(hi[1], MAX(sp[i].segment[1], sp[i].segment[3]) + r);
			}
			int32_t c0[2];
			int32_t c1[2];
			for (int k = 0; k < 2; k++) {
				c0[k] = MAX(win_lo[k], int32_t(Math::floor(lo[k] / tp.cell)));
				c1[k] = MIN(win_lo[k] + tp.size, int32_t(Math::ceil(hi[k] / tp.cell)) + 1);
			}
			if (c1[0] > c0[0] && c1[1] > c0[1]) {
				rects.push_back(Rect(c0[0], c0[1], c1[0] - c0[0], c1[1] - c0[1], false));
			}
		}
	}
	if (!rects.is_empty()) {
		rd->draw_command_begin_label("FellowEngine snow tracks");
		RD::ComputeListID list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, tracks_pipeline);
		rd->compute_list_bind_uniform_set(list, tracks_set, 0);
		for (uint32_t i = 0; i < rects.size(); i++) {
			const Rect &r = rects[i];
			TracksPush rp = tp;
			rp.region_lo[0] = r.lo[0];
			rp.region_lo[1] = r.lo[1];
			rp.region_size[0] = r.size[0];
			rp.region_size[1] = r.size[1];
			if (!r.clear) {
				// The strips are cleared already: the texels hold this window's cells.
				rp.prev_origin[0] = tp.origin[0];
				rp.prev_origin[1] = tp.origin[1];
			}
			// The stamps go into the last rectangle only (a strip and the stamps' bounds may overlap; the stamps'
			// bounds, when there are stamps, are the last).
			bool last = i + 1 == rects.size();
			rp.stamp_count = last ? tp.stamp_count : 0;
			rd->compute_list_set_push_constant(list, &rp, sizeof(rp));
			rd->compute_list_dispatch(list, (r.size[0] + 7) / 8, (r.size[1] + 7) / 8, 1);
			if (!last) {
				rd->compute_list_add_barrier(list);
			}
		}
		rd->compute_list_end();
		rd->draw_command_end_label();
	}
	r_state.tracks_texture = tracks_tex;
}

void FellowRain3D::_rt_free_tracks() {
	if (!rd) {
		return;
	}
	if (tracks_set.is_valid() && rd->uniform_set_is_valid(tracks_set)) {
		rd->free_rid(tracks_set);
	}
	tracks_set = RID();
	tracks_set_occlusion = RID();
	if (tracks_tex.is_valid()) {
		rd->free_rid(tracks_tex);
		tracks_tex = RID();
	}
	tracks_size = 0;
}

// ------------------------------------------------------------------ settings

void FellowRain3D::set_rain(float p_rain) {
	rain = CLAMP(p_rain, 0.0f, 1.0f);
}

void FellowRain3D::set_precipitation(Precipitation p_precipitation) {
	precipitation = p_precipitation;
	_update_children();
}

#define RAIN_CHILD_SETTER(m_func, m_type, m_member, m_expr) \
	void FellowRain3D::m_func(m_type p_value) { \
		m_member = m_expr; \
		_update_children(); \
	}

void FellowRain3D::set_wind(const Vector3 &p_wind) {
	wind = p_wind;
	wind_applied = p_wind;
	_update_children();
}
RAIN_CHILD_SETTER(set_area, float, area, MAX(1.0f, p_value))
RAIN_CHILD_SETTER(set_fall_height, float, fall_height, MAX(1.0f, p_value))
RAIN_CHILD_SETTER(set_max_drops, int, max_drops, CLAMP(p_value, 1, 1000000))
RAIN_CHILD_SETTER(set_drop_speed, float, drop_speed, MAX(0.1f, p_value))
RAIN_CHILD_SETTER(set_drop_size, const Vector2 &, drop_size, p_value)
RAIN_CHILD_SETTER(set_drop_color, const Color &, drop_color, p_value)
RAIN_CHILD_SETTER(set_streak_width, float, streak_width, MAX(0.0f, p_value))
RAIN_CHILD_SETTER(set_splashes, bool, splashes, p_value)
RAIN_CHILD_SETTER(set_occlusion_size, float, occlusion_size, MAX(1.0f, p_value))
RAIN_CHILD_SETTER(set_occlusion_height, float, occlusion_height, MAX(1.0f, p_value))
RAIN_CHILD_SETTER(set_occlusion_resolution, int, occlusion_resolution, p_value)
RAIN_CHILD_SETTER(set_occlusion_always_update, bool, occlusion_always_update, p_value)
RAIN_CHILD_SETTER(set_flake_speed, float, flake_speed, MAX(0.05f, p_value))
RAIN_CHILD_SETTER(set_flake_size, float, flake_size, MAX(0.001f, p_value))
RAIN_CHILD_SETTER(set_flake_flutter, float, flake_flutter, MAX(0.0f, p_value))
RAIN_CHILD_SETTER(set_flake_wind_influence, float, flake_wind_influence, MAX(0.0f, p_value))
RAIN_CHILD_SETTER(set_flake_color, const Color &, flake_color, p_value)

void FellowRain3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_precipitation", "precipitation"), &FellowRain3D::set_precipitation);
	ClassDB::bind_method(D_METHOD("get_precipitation"), &FellowRain3D::get_precipitation);
	ClassDB::bind_method(D_METHOD("set_snow", "snow"), &FellowRain3D::set_snow);
	ClassDB::bind_method(D_METHOD("get_snow"), &FellowRain3D::get_snow);
	ClassDB::bind_method(D_METHOD("set_snow_follows_precipitation", "enable"), &FellowRain3D::set_snow_follows_precipitation);
	ClassDB::bind_method(D_METHOD("is_snow_following_precipitation"), &FellowRain3D::is_snow_following_precipitation);
	ClassDB::bind_method(D_METHOD("set_snowing_time", "seconds"), &FellowRain3D::set_snowing_time);
	ClassDB::bind_method(D_METHOD("get_snowing_time"), &FellowRain3D::get_snowing_time);
	ClassDB::bind_method(D_METHOD("set_melting_time", "seconds"), &FellowRain3D::set_melting_time);
	ClassDB::bind_method(D_METHOD("get_melting_time"), &FellowRain3D::get_melting_time);
	ClassDB::bind_method(D_METHOD("set_snow_sparkle", "sparkle"), &FellowRain3D::set_snow_sparkle);
	ClassDB::bind_method(D_METHOD("get_snow_sparkle"), &FellowRain3D::get_snow_sparkle);
	ClassDB::bind_method(D_METHOD("set_rainbow", "rainbow"), &FellowRain3D::set_rainbow);
	ClassDB::bind_method(D_METHOD("get_rainbow"), &FellowRain3D::get_rainbow);
	ClassDB::bind_method(D_METHOD("set_freeze", "freeze"), &FellowRain3D::set_freeze);
	ClassDB::bind_method(D_METHOD("get_freeze"), &FellowRain3D::get_freeze);
	ClassDB::bind_method(D_METHOD("set_frost", "frost"), &FellowRain3D::set_frost);
	ClassDB::bind_method(D_METHOD("get_frost"), &FellowRain3D::get_frost);
	ClassDB::bind_method(D_METHOD("set_freezing_time", "seconds"), &FellowRain3D::set_freezing_time);
	ClassDB::bind_method(D_METHOD("set_drying_patches", "patches"), &FellowRain3D::set_drying_patches);
	ClassDB::bind_method(D_METHOD("get_drying_patches"), &FellowRain3D::get_drying_patches);
	ClassDB::bind_method(D_METHOD("set_wind_driven", "amount"), &FellowRain3D::set_wind_driven);
	ClassDB::bind_method(D_METHOD("get_wind_driven"), &FellowRain3D::get_wind_driven);
	ClassDB::bind_method(D_METHOD("set_slush", "slush"), &FellowRain3D::set_slush);
	ClassDB::bind_method(D_METHOD("get_slush"), &FellowRain3D::get_slush);
	ClassDB::bind_method(D_METHOD("set_streak_width", "width"), &FellowRain3D::set_streak_width);
	ClassDB::bind_method(D_METHOD("get_streak_width"), &FellowRain3D::get_streak_width);
	ClassDB::bind_method(D_METHOD("get_freezing_time"), &FellowRain3D::get_freezing_time);
	ClassDB::bind_method(D_METHOD("set_shafts", "shafts"), &FellowRain3D::set_shafts);
	ClassDB::bind_method(D_METHOD("get_shafts"), &FellowRain3D::get_shafts);
	ClassDB::bind_method(D_METHOD("set_showers", "showers"), &FellowRain3D::set_showers);
	ClassDB::bind_method(D_METHOD("get_showers"), &FellowRain3D::get_showers);
	ClassDB::bind_method(D_METHOD("get_rain_here"), &FellowRain3D::get_rain_here);
	ClassDB::bind_method(D_METHOD("set_flake_speed", "speed"), &FellowRain3D::set_flake_speed);
	ClassDB::bind_method(D_METHOD("get_flake_speed"), &FellowRain3D::get_flake_speed);
	ClassDB::bind_method(D_METHOD("set_flake_size", "size"), &FellowRain3D::set_flake_size);
	ClassDB::bind_method(D_METHOD("get_flake_size"), &FellowRain3D::get_flake_size);
	ClassDB::bind_method(D_METHOD("set_flake_flutter", "metres"), &FellowRain3D::set_flake_flutter);
	ClassDB::bind_method(D_METHOD("get_flake_flutter"), &FellowRain3D::get_flake_flutter);
	ClassDB::bind_method(D_METHOD("set_flake_wind_influence", "influence"), &FellowRain3D::set_flake_wind_influence);
	ClassDB::bind_method(D_METHOD("get_flake_wind_influence"), &FellowRain3D::get_flake_wind_influence);
	ClassDB::bind_method(D_METHOD("set_flake_color", "color"), &FellowRain3D::set_flake_color);
	ClassDB::bind_method(D_METHOD("get_flake_color"), &FellowRain3D::get_flake_color);
	ClassDB::bind_method(D_METHOD("set_tracks", "enable"), &FellowRain3D::set_tracks);
	ClassDB::bind_method(D_METHOD("get_tracks"), &FellowRain3D::get_tracks);
	ClassDB::bind_method(D_METHOD("set_track_window", "metres"), &FellowRain3D::set_track_window);
	ClassDB::bind_method(D_METHOD("get_track_window"), &FellowRain3D::get_track_window);
	ClassDB::bind_method(D_METHOD("set_track_resolution", "resolution"), &FellowRain3D::set_track_resolution);
	ClassDB::bind_method(D_METHOD("get_track_resolution"), &FellowRain3D::get_track_resolution);
	ClassDB::bind_method(D_METHOD("set_snow_depth", "metres"), &FellowRain3D::set_snow_depth);
	ClassDB::bind_method(D_METHOD("get_snow_depth"), &FellowRain3D::get_snow_depth);
	ClassDB::bind_method(D_METHOD("set_track_refill_time", "seconds"), &FellowRain3D::set_track_refill_time);
	ClassDB::bind_method(D_METHOD("get_track_refill_time"), &FellowRain3D::get_track_refill_time);
	ClassDB::bind_method(D_METHOD("stamp", "from", "to", "radius", "depth"), &FellowRain3D::stamp, DEFVAL(-1.0f));
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
	ClassDB::bind_method(D_METHOD("set_wind_from_scene", "enable"), &FellowRain3D::set_wind_from_scene);
	ClassDB::bind_method(D_METHOD("is_wind_from_scene"), &FellowRain3D::is_wind_from_scene);
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
	ADD_PROPERTY(PropertyInfo(Variant::INT, "precipitation", PROPERTY_HINT_ENUM, "Rain,Snow"), "set_precipitation", "get_precipitation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rain", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_rain", "get_rain");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "wind", PROPERTY_HINT_NONE, "suffix:m/s"), "set_wind", "get_wind");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "wind_from_scene"), "set_wind_from_scene", "is_wind_from_scene");
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
	ADD_GROUP("Snow", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "snow", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_snow", "get_snow");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "snow_follows_precipitation"), "set_snow_follows_precipitation", "is_snow_following_precipitation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "snowing_time", PROPERTY_HINT_RANGE, "0,36000,0.1,suffix:s"), "set_snowing_time", "get_snowing_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "melting_time", PROPERTY_HINT_RANGE, "0,36000,0.1,suffix:s"), "set_melting_time", "get_melting_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "snow_sparkle", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_snow_sparkle", "get_snow_sparkle");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "rainbow", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_rainbow", "get_rainbow");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shafts", PROPERTY_HINT_RANGE, "0,4,0.01,or_greater"), "set_shafts", "get_shafts");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "showers", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_showers", "get_showers");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "freeze", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_freeze", "get_freeze");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "frost", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_frost", "get_frost");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "freezing_time", PROPERTY_HINT_RANGE, "0,36000,0.1,suffix:s"), "set_freezing_time", "get_freezing_time");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "drying_patches", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_drying_patches", "get_drying_patches");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_driven", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_wind_driven", "get_wind_driven");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "slush", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_slush", "get_slush");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "streak_width", PROPERTY_HINT_RANGE, "0,0.05,0.0001,suffix:m"), "set_streak_width", "get_streak_width");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "snow_depth", PROPERTY_HINT_RANGE, "0.001,2,0.001,suffix:m"), "set_snow_depth", "get_snow_depth");
	ADD_SUBGROUP("Tracks", "track");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "tracks"), "set_tracks", "get_tracks");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "track_window", PROPERTY_HINT_RANGE, "4,1024,0.1,suffix:m"), "set_track_window", "get_track_window");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "track_resolution", PROPERTY_HINT_ENUM, "512:512,1024:1024,2048:2048,4096:4096"), "set_track_resolution", "get_track_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "track_refill_time", PROPERTY_HINT_RANGE, "0,36000,0.1,suffix:s"), "set_track_refill_time", "get_track_refill_time");
	ADD_SUBGROUP("Flakes", "flake_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flake_speed", PROPERTY_HINT_RANGE, "0.05,5,0.01,suffix:m/s"), "set_flake_speed", "get_flake_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flake_size", PROPERTY_HINT_RANGE, "0.001,0.2,0.001,suffix:m"), "set_flake_size", "get_flake_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flake_flutter", PROPERTY_HINT_RANGE, "0,1,0.01,suffix:m"), "set_flake_flutter", "get_flake_flutter");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flake_wind_influence", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:1/s"), "set_flake_wind_influence", "get_flake_wind_influence");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "flake_color"), "set_flake_color", "get_flake_color");
	ADD_GROUP("Shelter", "occlusion_");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_size", PROPERTY_HINT_RANGE, "1,1000,0.1,suffix:m"), "set_occlusion_size", "get_occlusion_size");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "occlusion_height", PROPERTY_HINT_RANGE, "1,1000,0.1,suffix:m"), "set_occlusion_height", "get_occlusion_height");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "occlusion_resolution", PROPERTY_HINT_ENUM, "256:256,512:512,1024:1024,2048:2048,4096:4096"), "set_occlusion_resolution", "get_occlusion_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "occlusion_always_update"), "set_occlusion_always_update", "get_occlusion_always_update");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time", PROPERTY_HINT_NONE, "suffix:s", PROPERTY_USAGE_NONE), "set_time", "get_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "time_auto"), "set_time_auto", "is_time_auto");

	BIND_ENUM_CONSTANT(PRECIPITATION_RAIN);
	BIND_ENUM_CONSTANT(PRECIPITATION_SNOW);
}

FellowRain3D::FellowRain3D() {
	ripple_texture.instantiate();
	wind_applied = wind;
}

void FellowRain3D::_rt_destroy() {
	if (gpu_ready) {
		_rt_free();
	}
	if (shader_ripples.is_valid()) {
		rd->free_rid(shader_ripples);
	}
	if (shader_tail.is_valid()) {
		rd->free_rid(shader_tail);
	}
	_rt_free_tracks();
	if (tracks_stamps.is_valid()) {
		rd->free_rid(tracks_stamps);
	}
	if (tracks_sampler.is_valid()) {
		rd->free_rid(tracks_sampler);
	}
	if (tracks_shader.is_valid()) {
		rd->free_rid(tracks_shader);
	}
}

FellowRain3D::~FellowRain3D() {
	if (active == this) {
		active = nullptr;
	}
	if (RS::get_singleton() && rd) {
		// FellowEngine: the RenderingDevice's resources are freed on the render thread. With a separate one
		// (--render-thread separate) this waits for it: the calls queued before for this node run first, and none of them
		// runs on a node half destroyed.
		RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowRain3D::_rt_destroy));
		if (!RS::get_singleton()->is_on_render_thread()) {
			RS::get_singleton()->sync();
		}
	}
}
