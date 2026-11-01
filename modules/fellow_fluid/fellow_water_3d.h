/**************************************************************************/
/*  fellow_water_3d.h                                                     */
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

#include "scene/3d/mesh_instance_3d.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/texture_rd.h"
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_device_binds.h"

class DirectionalLight3D;
class FellowShallowWater3D;

// FellowEngine water body (lakes, rivers, the sea): the surface of a body of water at the node's height, to the
// horizon or within `extent`, with spectral wind waves (Tessendorf FFT in up to four cascades, on the GPU), and the
// GPU shallow-water simulation of a FellowShallowWater3D blended in where it covers the body (the simulated surface,
// its flow carrying the waves, its foam).
//
// Shading in one transparent material: reflections traced in screen space with the sky's (Godot's IBL) as fallback,
// Fresnel, refraction of the scene below through the screen texture, absorption along the light's path down and the
// view's path up, in-scattering lit by the scene's lights (shadows included), foam (breaking waves, shores, the
// simulation's), caustics on the refracted bottom (from the waves' own slopes), roughness from the slope variance the
// pixel does not resolve (a far surface reflects a broad glitter path, not a mirror image).
//
// The surface mesh is a polar grid around the camera (vertex spacing growing with distance), its displacement
// band-limited to the spacing so it does not alias or swim.
class FellowWater3D : public Node3D {
	GDCLASS(FellowWater3D, Node3D);

public:
	static constexpr int MAX_CASCADES = 4;

	enum DebugView {
		DEBUG_VIEW_OFF,
		DEBUG_VIEW_REFLECTIONS, // r: screen-space hit weight, g: Fresnel
		DEBUG_VIEW_FOAM, // r: amount, g: drawn foam, b: shore term
		DEBUG_VIEW_REFRACTION, // the scene below after absorption (and caustics)
		DEBUG_VIEW_NORMALS,
		DEBUG_VIEW_SIMULATION, // r: simulation weight, g: its foam, b: flow speed / 2 m/s
		DEBUG_VIEW_ROUGHNESS, // r: unresolved slope deviation, g: roughness
		DEBUG_VIEW_REFLECTION_COST, // r: depth lookups / (2 x reflection_steps), g: screen-space hit weight
	};

private:
	struct PushConstant {
		int32_t size;
		int32_t log2_size;
		int32_t cascades;
		int32_t fft_vertical;
		float lengths[4];
		float time_fraction;
		float repeat_period;
		float gravity;
		float choppiness;
		float foam_decay;
		float foam_threshold;
		float foam_gain;
		float caustic_depth0;
		float sun_direction[4];
		int32_t dst_size;
		int32_t pad[3];
	};
	static_assert(sizeof(PushConstant) == 96);

	enum Kernel {
		KERNEL_EVOLVE,
		KERNEL_FFT,
		KERNEL_ASSEMBLE,
		KERNEL_DOWNSAMPLE,
		KERNEL_CAUSTICS,
		KERNEL_MAX,
	};

	// Waves.
	float wind_speed = 6.0f; // m/s at 10 m
	float wind_direction = 0.0f; // degrees, 0 = towards +X
	float fetch = 3.0f; // km
	float wave_scale = 1.0f;
	float choppiness = 1.0f;
	Vector4 cascade_lengths = Vector4(160.0f, 27.0f, 5.0f, 0.0f); // m; 0 = unused
	int wave_resolution = 256;
	float repeat_period = 200.0f; // s
	float whitecap_threshold = 0.45f; // Jacobian
	float foam_decay = 0.6f; // 1/s
	float foam_gain = 0.15f;

	// Surface and shading.
	Vector2 extent; // m, centred on the node; zero = to the horizon
	Vector3 absorption = Vector3(0.55f, 0.16f, 0.30f); // 1/m: lake water (pure water's red absorption, dissolved matter's blue)
	Color scattering_color = Color(0.010f, 0.032f, 0.022f);
	Color foam_color = Color(0.85f, 0.87f, 0.88f);
	float refraction_strength = 0.25f;
	float roughness = 0.04f;
	float shore_foam_depth = 0.15f; // m
	float caustics_strength = 0.7f;
	Vector2 caustics_depths = Vector2(0.6f, 2.5f);
	bool screen_space_reflections = true;
	int reflection_steps = 32;
	float reflection_distance = 80.0f; // m
	float reflection_min_fresnel = 0.04f; // below: the IBL only, no march
	int grid_segments = 256;
	float grid_near = 0.25f; // m: radius of the innermost ring
	float grid_far = 20000.0f; // m

	// Simulation.
	NodePath simulation_path;
	float simulation_wave_scale = 0.35f;
	float simulation_blend = 4.0f; // m
	float flow_period = 1.6f; // s

	// The scene's wind (decision 0127).
	bool wind_from_scene = false;
	float gust_ripples = 1.0f;

	NodePath sun_path;
	DebugView debug_view = DEBUG_VIEW_OFF;

	double time = 0.0;
	bool time_auto = true;

	// Main-thread state.
	MeshInstance3D *mesh_instance = nullptr;
	Ref<ShaderMaterial> material;
	Ref<Shader> shader;
	bool shader_hierarchical = false; // the shader's depth texture has a mipmap filter (reflections on)
	Ref<ImageTexture> foam_noise;
	ObjectID sim_id;
	ObjectID sun_id;
	bool spectrum_dirty = true;
	bool mesh_dirty = true;
	Vector3 last_sun_direction;
	float last_rain_amount = 0.0f;
	ObjectID last_rain_id;
	float last_rain_tile = 0.0f;
	ObjectID last_wind_id;
	float last_gust_strength = 0.0f;
	double last_dispatch_time = -1.0;

	// Render-thread state.
	RenderingDevice *rd = nullptr;
	Ref<RDShaderFile> shader_file;
	RID shaders[KERNEL_MAX];
	RID pipelines[KERNEL_MAX];
	RID h0_tex;
	RID spectrum_tex[2];
	RID displacement_tex;
	RID slope_tex;
	RID caustics_tex;
	Vector<RID> displacement_mips;
	Vector<RID> slope_mips;
	Vector<RID> caustics_mips;
	RID set_evolve;
	RID set_fft;
	RID set_assemble;
	RID set_caustics;
	Vector<RID> sets_downsample; // per texture and level
	int allocated_size = 0;
	int allocated_cascades = 0;
	int allocated_levels = 0;
	bool gpu_ready = false;
	bool gpu_failed = false;
	double waves_gpu_ms = 0.0;
	Ref<Texture2DArrayRD> displacement_texture;
	Ref<Texture2DArrayRD> slope_texture;
	Ref<Texture2DArrayRD> caustics_texture;

	int _cascade_count() const;
	Vector<uint8_t> _build_spectrum(int p_size, int p_cascades) const;
	void _update_mesh();
	void _update_material();
	void _update_simulation_link();
	DirectionalLight3D *_find_sun() const;
	Vector3 _sun_direction();
	void _make_foam_noise();

	void _rt_create(int p_size, int p_cascades);
	void _rt_free();
	void _rt_upload_spectrum(Vector<uint8_t> p_data);
	void _rt_dispatch(PackedByteArray p_push);
	void _rt_read_timestamps();
	RID _rt_uniform_set(Kernel p_kernel, const Vector<RID> &p_images);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_wind_speed(float p_speed);
	float get_wind_speed() const { return wind_speed; }
	void set_wind_direction(float p_degrees);
	float get_wind_direction() const { return wind_direction; }
	void set_wind_from_scene(bool p_enable) { wind_from_scene = p_enable; }
	bool is_wind_from_scene() const { return wind_from_scene; }
	void set_gust_ripples(float p_strength) { gust_ripples = MAX(0.0f, p_strength); }
	float get_gust_ripples() const { return gust_ripples; }
	void set_fetch(float p_km);
	float get_fetch() const { return fetch; }
	void set_wave_scale(float p_scale);
	float get_wave_scale() const { return wave_scale; }
	void set_choppiness(float p_choppiness);
	float get_choppiness() const { return choppiness; }
	void set_cascade_lengths(const Vector4 &p_lengths);
	Vector4 get_cascade_lengths() const { return cascade_lengths; }
	void set_wave_resolution(int p_resolution);
	int get_wave_resolution() const { return wave_resolution; }
	void set_repeat_period(float p_seconds);
	float get_repeat_period() const { return repeat_period; }
	void set_whitecap_threshold(float p_threshold) { whitecap_threshold = p_threshold; }
	float get_whitecap_threshold() const { return whitecap_threshold; }
	void set_foam_decay(float p_decay) { foam_decay = MAX(0.0f, p_decay); }
	float get_foam_decay() const { return foam_decay; }
	void set_foam_gain(float p_gain) { foam_gain = MAX(0.0f, p_gain); }
	float get_foam_gain() const { return foam_gain; }

	void set_extent(const Vector2 &p_extent);
	Vector2 get_extent() const { return extent; }
	void set_absorption(const Vector3 &p_absorption);
	Vector3 get_absorption() const { return absorption; }
	void set_scattering_color(const Color &p_color);
	Color get_scattering_color() const { return scattering_color; }
	void set_foam_color(const Color &p_color);
	Color get_foam_color() const { return foam_color; }
	void set_refraction_strength(float p_strength);
	float get_refraction_strength() const { return refraction_strength; }
	void set_roughness(float p_roughness);
	float get_roughness() const { return roughness; }
	void set_shore_foam_depth(float p_depth);
	float get_shore_foam_depth() const { return shore_foam_depth; }
	void set_caustics_strength(float p_strength);
	float get_caustics_strength() const { return caustics_strength; }
	void set_caustics_depths(const Vector2 &p_depths);
	Vector2 get_caustics_depths() const { return caustics_depths; }
	void set_screen_space_reflections(bool p_enabled);
	bool get_screen_space_reflections() const { return screen_space_reflections; }
	void set_reflection_steps(int p_steps);
	int get_reflection_steps() const { return reflection_steps; }
	void set_reflection_distance(float p_distance);
	float get_reflection_distance() const { return reflection_distance; }
	void set_reflection_min_fresnel(float p_fresnel);
	float get_reflection_min_fresnel() const { return reflection_min_fresnel; }
	void set_grid_segments(int p_segments);
	int get_grid_segments() const { return grid_segments; }
	void set_grid_near(float p_near);
	float get_grid_near() const { return grid_near; }
	void set_grid_far(float p_far);
	float get_grid_far() const { return grid_far; }

	void set_simulation_path(const NodePath &p_path);
	NodePath get_simulation_path() const { return simulation_path; }
	void set_simulation_wave_scale(float p_scale);
	float get_simulation_wave_scale() const { return simulation_wave_scale; }
	void set_simulation_blend(float p_blend);
	float get_simulation_blend() const { return simulation_blend; }
	void set_flow_period(float p_period);
	float get_flow_period() const { return flow_period; }
	void set_sun_path(const NodePath &p_path);
	NodePath get_sun_path() const { return sun_path; }

	void set_debug_view(DebugView p_view);
	DebugView get_debug_view() const { return debug_view; }
	void set_time(double p_time) { time = p_time; }
	double get_time() const { return time; }
	void set_time_auto(bool p_auto) { time_auto = p_auto; }
	bool is_time_auto() const { return time_auto; }

	double get_waves_gpu_time_ms() const { return waves_gpu_ms; }
	float get_significant_wave_height() const;
	Ref<Texture2DArrayRD> get_displacement_texture() const { return displacement_texture; }
	Ref<Texture2DArrayRD> get_slope_texture() const { return slope_texture; }
	MeshInstance3D *get_surface_mesh_instance() const { return mesh_instance; }
	Ref<Image> debug_get_spectrum_image(int p_cascade) const;
	Ref<Image> debug_get_displacement_image(int p_cascade);

	FellowWater3D();
	~FellowWater3D();
};

VARIANT_ENUM_CAST(FellowWater3D::DebugView);
