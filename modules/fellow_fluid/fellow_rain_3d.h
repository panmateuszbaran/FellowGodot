/**************************************************************************/
/*  fellow_rain_3d.h                                                      */
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

#include "scene/3d/gpu_particles_3d.h"
#include "scene/3d/gpu_particles_collision_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/particle_process_material.h"
#include "scene/resources/texture_rd.h"
#include "servers/rendering/renderer_rd/effects/fellow_weather.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_device_binds.h"

// FellowEngine rain (docs/decisions/0124-rain.md): drops falling around the camera (GPU particles colliding with the
// scene as seen from above, splashing where they land), and the renderer told to wet every opaque material where the
// rain reaches it (Forward+: darker porous surfaces, glossier ones, puddles on flat ground with the drops' ripples). The
// same heightfield (a GPUParticlesCollisionHeightField3D following the camera) keeps the drops off and the ground dry
// under roofs. A FellowWater3D shows the ripples too. One node at a time is active (the last one to enter the tree).
//
// Snow (docs/decisions/0128-snow.md): with `precipitation` SNOW the same node lets flakes drift down around the camera
// (following the scene's wind and its gusts, fluttering) and covers what faces up with snow where it falls (`snow`).
class FellowRain3D : public Node3D {
	GDCLASS(FellowRain3D, Node3D);

public:
	static void fellow_bake_shader_codes(Vector<String> &r_codes); // milestone 61: the drops' and the flakes' codes
	enum Precipitation {
		PRECIPITATION_RAIN,
		PRECIPITATION_SNOW,
	};

private:
	struct PushConstant {
		int32_t size;
		int32_t levels;
		float tile;
		float time;
		float rain;
		float amplitude;
		float pad[2];
	};
	static_assert(sizeof(PushConstant) == 32);

	// The snow tracks' pass (decision 0130): fellow_snow_tracks.glsl's Params.
	struct TracksPush {
		int32_t origin[2];
		int32_t prev_origin[2];
		int32_t region_lo[2];
		int32_t region_size[2];
		int32_t size;
		int32_t stamp_count;
		float cell;
		float refill;
		float occ_origin[2];
		float occ_inv[2];
		float occ_y;
		float occ_half;
		float occ_enabled;
		float pad;
	};
	static_assert(sizeof(TracksPush) == 80);

public:
	// A capsule pressed into the snow: x0, z0, x1, z1; radius, depth, the presser's height, its contact height (< 0:
	// always).
	struct TrackStamp {
		float segment[4];
		float shape[4];
	};
	static constexpr int MAX_TRACK_STAMPS = 256;

private:
	static FellowRain3D *active;

	Precipitation precipitation = PRECIPITATION_RAIN;
	float rain = 0.6f; // 0..1: how hard it rains (or snows)
	float wetness = 1.0f; // 0..1
	bool wetness_follows_rain = false;
	float wetting_time = 60.0f; // s to get fully wet in full rain
	float drying_time = 600.0f; // s to dry
	float puddles = 0.5f; // 0..1
	Vector3 wind = Vector3(1.0f, 0.0f, 0.5f); // m/s
	bool wind_from_scene = true; // a FellowWind3D's, where there is one
	Vector3 wind_applied; // what the drops were last given
	float area = 40.0f; // m: side of the box the drops fall in around the camera
	float fall_height = 14.0f; // m above the camera the drops start
	int max_drops = 24000;
	float drop_speed = 9.0f; // m/s
	Vector2 drop_size = Vector2(0.024f, 0.55f); // m: width, length (the streak a frame's exposure sees)
	Color drop_color = Color(0.85f, 0.88f, 0.92f, 0.4f);
	bool splashes = true;
	float occlusion_size = 80.0f; // m
	float occlusion_height = 80.0f; // m
	int occlusion_resolution = 1024;
	bool occlusion_always_update = false;
	float ripple_tile = 1.5f; // m
	float ripple_strength = 1.0f;
	float snow = 0.0f; // 0..1: the snow's cover
	bool snow_follows_precipitation = false;
	float snowing_time = 900.0f; // s to cover the ground in full snowfall
	float melting_time = 1800.0f; // s to melt
	float snow_sparkle = 1.0f;
	float rainbow = 1.0f; // decision 0134: the rainbow's strength
	float shafts = 1.0f; // decision 0135: the rain shafts' strength under the volumetric clouds
	float showers = 0.0f; // decision 0137: 1: it rains at the camera only as the clouds above let it (0: everywhere)
	float rain_here = 1.0f; // the share of the rain falling at the camera (showers), eased over SHOWER_TIME
	bool rain_here_seen = false;
	static constexpr float SHOWER_TIME = 2.0f; // s
	float freeze = 0.0f; // decision 0136: 0..1, how much of the wet surfaces' water is ice
	float frost = 0.0f; // 0..1: hoarfrost on what is exposed to the sky
	float freezing_time = 600.0f; // s for the water to freeze at -5 degrees (a weather controller's ground model)
	float drying_patches = 1.0f; // decision 0138: 0..1, the ground wets and dries in patches, the puddles shrinking
	float wind_driven = 1.0f; // decision 0139: 0..1, the shelter follows the drops' slant in the wind
	float slush = 0.0f; // decision 0140: 0..1, the snow cover soaked with its meltwater
	float streak_width = 0.0f; // decision 0141: m, the drops' own width (0: drop_size's)
	float rain_seen = 0.0f; // the rain lately (it fades over RAINBOW_TIME once the rain stops: the rain moving away)
	static constexpr float RAINBOW_TIME = 90.0f; // s
	float flake_speed = 1.1f; // m/s
	float flake_size = 0.018f; // m
	float flake_flutter = 0.12f; // m: how far a flake swings about its path
	float flake_wind_influence = 1.5f; // 1/s: how fast the flakes take the scene's wind
	bool tracks = true; // decision 0130
	float track_window = 48.0f; // m: the side of the tracks' window around the camera (2048 cells: 2.3 cm each)
	int track_resolution = 2048;
	float snow_depth = 0.08f; // m
	float track_refill_time = 900.0f; // s for full snowfall to fill a track of the snow's depth
	LocalVector<TrackStamp> pending_stamps;
	double track_refill_accum = 0.0;
	Color flake_color = Color(0.95f, 0.96f, 1.0f, 0.9f);
	double time = 0.0;
	bool time_auto = true;

	GPUParticlesCollisionHeightField3D *occlusion = nullptr;
	GPUParticles3D *drops = nullptr;
	GPUParticles3D *splash = nullptr;
	Ref<ParticleProcessMaterial> drop_process;
	// The flakes' process materials, without and with the scene's wind (decision 0156): made with the drops', so their
	// shaders compile when the node is set up (a process material whose flags change compiles a new shader).
	Ref<ParticleProcessMaterial> flake_process[2];
	// One particle each of the process materials (the drops', the splashes', the flakes' two), drawing nothing, for the
	// first frames: a particles shader is compiled when particles first use it (decision 0156).
	LocalVector<GPUParticles3D *> process_warmers;
	int process_warm_frames = 0;
	void _free_process_warmers();
	Ref<ParticleProcessMaterial> splash_process;
	Ref<Shader> drop_shader;
	Ref<ShaderMaterial> drop_material;
	Ref<ShaderMaterial> splash_material;
	Ref<Shader> flake_shader;
	Ref<ShaderMaterial> flake_material;
	double last_time = -1.0;
	// The state last sent to the renderer (milestone 52: not sent again in the still editor).
	int sent_enabled = -1;
	PackedByteArray sent_push;
	PackedByteArray sent_state;
	float sent_ripple_rain = 1.0f; // the ripples' last pass: its rain (a first pass, then none while it is 0)
	int32_t sent_tracks_origin[2] = { INT32_MAX, INT32_MAX };

	// Render thread.
	RenderingDevice *rd = nullptr;
	Ref<RDShaderFile> shader_file;
	RID shader_ripples;
	RID shader_tail;
	RID pipeline_ripples;
	RID pipeline_tail;
	RID ripple_tex;
	Vector<RID> ripple_mips;
	RID set_ripples;
	RID set_tail;
	int ripple_size = 256;
	bool gpu_ready = false;
	bool gpu_failed = false;
	Ref<Texture2DRD> ripple_texture;
	// The snow's tracks.
	Ref<RDShaderFile> tracks_shader_file;
	RID tracks_shader;
	RID tracks_pipeline;
	RID tracks_tex;
	RID tracks_stamps;
	RID tracks_sampler;
	RID tracks_set;
	RID tracks_set_occlusion; // the heightfield texture tracks_set was built with
	int tracks_size = 0;
	float tracks_cell = 0.0f; // m: the window's cell the texture holds (a new window size starts it afresh)
	int32_t tracks_prev_origin[2] = {};

	static constexpr float SNOW_LIFETIME = 8.0f; // s: a flake's life in the box around the camera

	bool _is_snow() const { return precipitation == PRECIPITATION_SNOW; }
	void _build_children();
	void _update_children();
	void _push_state();
	void _rt_create();
	void _rt_free();
	void _rt_destroy();
	void _rt_update(PackedByteArray p_push, bool p_enabled, bool p_ripples, PackedByteArray p_state, PackedByteArray p_tracks);
	void _rt_tracks(RendererRD::FellowWeather::State &r_state, const PackedByteArray &p_tracks);
	void _rt_free_tracks();
	bool _tracks_active() const;
	void _rt_disable();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;

	static FellowRain3D *get_active() { return active; }

	void set_precipitation(Precipitation p_precipitation);
	Precipitation get_precipitation() const { return precipitation; }
	void set_rain(float p_rain);
	float get_rain() const { return rain; }
	// What makes ripples on puddles and water: the rain's rate, none while it snows.
	// The rain falling here: at the camera, under showers (decision 0137) as the clouds let it.
	float get_ripple_rain() const { return precipitation == PRECIPITATION_RAIN ? rain * rain_here : 0.0f; }
	float get_rain_here() const { return rain_here; }
	void set_wetness(float p_wetness) { wetness = CLAMP(p_wetness, 0.0f, 1.0f); }
	float get_wetness() const { return wetness; }
	void set_wetness_follows_rain(bool p_enable) { wetness_follows_rain = p_enable; }
	bool is_wetness_following_rain() const { return wetness_follows_rain; }
	void set_wetting_time(float p_seconds) { wetting_time = MAX(0.0f, p_seconds); }
	float get_wetting_time() const { return wetting_time; }
	void set_drying_time(float p_seconds) { drying_time = MAX(0.0f, p_seconds); }
	float get_drying_time() const { return drying_time; }
	void set_puddles(float p_puddles) { puddles = CLAMP(p_puddles, 0.0f, 1.0f); }
	float get_puddles() const { return puddles; }
	void set_wind(const Vector3 &p_wind);
	Vector3 get_wind() const { return wind; }
	void set_wind_from_scene(bool p_enable) { wind_from_scene = p_enable; }
	bool is_wind_from_scene() const { return wind_from_scene; }
	void set_area(float p_area);
	float get_area() const { return area; }
	void set_fall_height(float p_height);
	float get_fall_height() const { return fall_height; }
	void set_max_drops(int p_count);
	int get_max_drops() const { return max_drops; }
	void set_drop_speed(float p_speed);
	float get_drop_speed() const { return drop_speed; }
	void set_drop_size(const Vector2 &p_size);
	Vector2 get_drop_size() const { return drop_size; }
	void set_drop_color(const Color &p_color);
	Color get_drop_color() const { return drop_color; }
	void set_splashes(bool p_enable);
	bool get_splashes() const { return splashes; }
	void set_occlusion_size(float p_size);
	float get_occlusion_size() const { return occlusion_size; }
	void set_occlusion_height(float p_height);
	float get_occlusion_height() const { return occlusion_height; }
	void set_occlusion_resolution(int p_resolution);
	int get_occlusion_resolution() const { return occlusion_resolution; }
	void set_occlusion_always_update(bool p_enable);
	bool get_occlusion_always_update() const { return occlusion_always_update; }
	void set_ripple_tile(float p_tile) { ripple_tile = MAX(0.1f, p_tile); }
	float get_ripple_tile() const { return ripple_tile; }
	void set_ripple_strength(float p_strength) { ripple_strength = MAX(0.0f, p_strength); }
	float get_ripple_strength() const { return ripple_strength; }
	void set_snow(float p_snow) { snow = CLAMP(p_snow, 0.0f, 1.0f); }
	float get_snow() const { return snow; }
	void set_snow_follows_precipitation(bool p_enable) { snow_follows_precipitation = p_enable; }
	bool is_snow_following_precipitation() const { return snow_follows_precipitation; }
	void set_snowing_time(float p_seconds) { snowing_time = MAX(0.0f, p_seconds); }
	float get_snowing_time() const { return snowing_time; }
	void set_melting_time(float p_seconds) { melting_time = MAX(0.0f, p_seconds); }
	float get_melting_time() const { return melting_time; }
	void set_snow_sparkle(float p_sparkle) { snow_sparkle = CLAMP(p_sparkle, 0.0f, 1.0f); }
	float get_snow_sparkle() const { return snow_sparkle; }
	void set_rainbow(float p_rainbow) { rainbow = MAX(0.0f, p_rainbow); }
	float get_rainbow() const { return rainbow; }
	void set_shafts(float p_shafts) { shafts = MAX(0.0f, p_shafts); }
	float get_shafts() const { return shafts; }
	void set_showers(float p_showers) { showers = CLAMP(p_showers, 0.0f, 1.0f); }
	float get_showers() const { return showers; }
	void set_freeze(float p_freeze) { freeze = CLAMP(p_freeze, 0.0f, 1.0f); }
	float get_freeze() const { return freeze; }
	void set_frost(float p_frost) { frost = CLAMP(p_frost, 0.0f, 1.0f); }
	float get_frost() const { return frost; }
	void set_freezing_time(float p_seconds) { freezing_time = MAX(0.0f, p_seconds); }
	void set_drying_patches(float p_patches) { drying_patches = CLAMP(p_patches, 0.0f, 1.0f); }
	float get_drying_patches() const { return drying_patches; }
	void set_wind_driven(float p_amount) { wind_driven = CLAMP(p_amount, 0.0f, 1.0f); }
	float get_wind_driven() const { return wind_driven; }
	void set_slush(float p_slush) { slush = CLAMP(p_slush, 0.0f, 1.0f); }
	float get_slush() const { return slush; }
	void set_streak_width(float p_width);
	float get_streak_width() const { return streak_width; }
	float get_freezing_time() const { return freezing_time; }
	void set_flake_speed(float p_speed);
	float get_flake_speed() const { return flake_speed; }
	void set_flake_size(float p_size);
	float get_flake_size() const { return flake_size; }
	void set_flake_flutter(float p_flutter);
	float get_flake_flutter() const { return flake_flutter; }
	void set_flake_wind_influence(float p_influence);
	float get_flake_wind_influence() const { return flake_wind_influence; }
	void set_flake_color(const Color &p_color);
	Color get_flake_color() const { return flake_color; }
	void set_tracks(bool p_enable);
	bool get_tracks() const { return tracks; }
	void set_track_window(float p_metres) { track_window = CLAMP(p_metres, 4.0f, 1024.0f); }
	float get_track_window() const { return track_window; }
	void set_track_resolution(int p_resolution);
	int get_track_resolution() const { return track_resolution; }
	void set_snow_depth(float p_metres) { snow_depth = MAX(0.001f, p_metres); }
	float get_snow_depth() const { return snow_depth; }
	void set_track_refill_time(float p_seconds) { track_refill_time = MAX(0.0f, p_seconds); }
	float get_track_refill_time() const { return track_refill_time; }
	// Presses a capsule from p_from to p_to (their x and z) of p_radius into the snow, p_depth deep (< 0: 0.8 of
	// snow_depth), once (applied next frame, within the window around the camera).
	void stamp(const Vector3 &p_from, const Vector3 &p_to, float p_radius, float p_depth = -1.0f);
	void add_track_stamp(const TrackStamp &p_stamp);
	void set_time(double p_time) { time = p_time; }
	double get_time() const { return time; }
	void set_time_auto(bool p_auto) { time_auto = p_auto; }
	bool is_time_auto() const { return time_auto; }

	Ref<Texture2DRD> get_ripple_texture() const { return ripple_texture; }
	GPUParticles3D *get_drops() const { return drops; }
	GPUParticlesCollisionHeightField3D *get_occlusion() const { return occlusion; }

	FellowRain3D();
	~FellowRain3D();
};

VARIANT_ENUM_CAST(FellowRain3D::Precipitation);
