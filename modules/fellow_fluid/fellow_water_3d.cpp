/**************************************************************************/
/*  fellow_water_3d.cpp                                                   */
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

#include "fellow_water_3d.h"

#include "fellow_editor_still.h"
#include "fellow_node_warnings.h"
#include "fellow_rain_3d.h"
#include "fellow_shallow_water_3d.h"
#include "fellow_water_shader.h"
#include "fellow_water_waves.glsl.gen.h"
#include "fellow_wind_3d.h"

#include "core/math/projection.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/3d/light_3d.h"
#include "scene/main/scene_tree.h"
#include "scene/main/viewport.h"
#include "scene/main/window.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/rendering_server.h"

static const char *wave_kernel_names[] = { "evolve", "fft", "assemble", "downsample", "caustics" };

static const double WATER_GRAVITY = 9.81;

// ------------------------------------------------------------------ spectrum (CPU)

static inline uint32_t water_pcg(uint32_t p_v) {
	uint32_t state = p_v * 747796405u + 2891336453u;
	uint32_t word = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
	return (word >> 22u) ^ word;
}

static inline double water_uniform(uint32_t p_key) {
	return (double(water_pcg(p_key)) + 0.5) / 4294967296.0;
}

// JONSWAP (fetch-limited wind sea, Hasselmann et al. 1973) with the Donelan-Banner directional spreading.
struct WaterSpectrum {
	double alpha = 0.0;
	double omega_peak = 1.0;
	double wind_angle = 0.0;

	WaterSpectrum(double p_wind, double p_fetch_m, double p_wind_angle) {
		double u = MAX(p_wind, 0.1);
		double f = MAX(p_fetch_m, 1.0);
		alpha = 0.076 * Math::pow(u * u / (f * WATER_GRAVITY), 0.22);
		omega_peak = 22.0 * Math::pow(WATER_GRAVITY * WATER_GRAVITY / (u * f), 1.0 / 3.0);
		wind_angle = p_wind_angle;
	}

	double jonswap(double p_omega) const {
		if (p_omega <= 1e-6) {
			return 0.0;
		}
		double sigma = p_omega <= omega_peak ? 0.07 : 0.09;
		double r = Math::exp(-(p_omega - omega_peak) * (p_omega - omega_peak) / (2.0 * sigma * sigma * omega_peak * omega_peak));
		double wr = omega_peak / p_omega;
		return alpha * WATER_GRAVITY * WATER_GRAVITY / Math::pow(p_omega, 5.0) * Math::exp(-1.25 * wr * wr * wr * wr) * Math::pow(3.3, r);
	}

	double spreading(double p_omega, double p_theta) const {
		double x = p_omega / omega_peak;
		double beta;
		if (x < 0.95) {
			beta = 2.61 * Math::pow(x, 1.3);
		} else if (x < 1.6) {
			beta = 2.28 * Math::pow(x, -1.3);
		} else {
			double eps = -0.4 + 0.8393 * Math::exp(-0.567 * Math::log(x * x));
			beta = Math::pow(10.0, eps);
		}
		double th = Math::fmod(p_theta - wind_angle + 3.0 * Math::PI, 2.0 * Math::PI) - Math::PI;
		double sech = 1.0 / Math::cosh(beta * th);
		return beta / (2.0 * Math::tanh(beta * Math::PI)) * sech * sech;
	}
};

int FellowWater3D::_cascade_count() const {
	int n = 0;
	for (int i = 0; i < MAX_CASCADES; i++) {
		if (cascade_lengths[i] > 0.0f) {
			n = i + 1;
		} else {
			break;
		}
	}
	return MAX(n, 1);
}

// h0(k) of every cascade: RGBA32F per texel (h0(k), conj(h0(-k))). Cascade c holds the wave numbers from
// 12 pi / L_c (six of its own fundamentals) to where the next, shorter cascade starts (the first from 0).
Vector<uint8_t> FellowWater3D::_build_spectrum(int p_size, int p_cascades) const {
	Vector<uint8_t> bytes;
	bytes.resize(int64_t(p_size) * p_size * p_cascades * 16);
	float *out = reinterpret_cast<float *>(bytes.ptrw());
	WaterSpectrum spectrum(wind_speed, fetch * 1000.0, Math::deg_to_rad(double(wind_direction)));
	LocalVector<double> h0;
	h0.resize(uint32_t(p_size) * p_size * 2);
	for (int c = 0; c < p_cascades; c++) {
		double len = cascade_lengths[c];
		double dk = 2.0 * Math::PI / len;
		double k_low = c == 0 ? 0.0 : 12.0 * Math::PI / len;
		double k_high = c + 1 < p_cascades ? 12.0 * Math::PI / double(cascade_lengths[c + 1]) : 1e30;
		for (int m = 0; m < p_size; m++) {
			for (int n = 0; n < p_size; n++) {
				double kx = (n - p_size / 2) * dk;
				double kz = (m - p_size / 2) * dk;
				double k = Math::sqrt(kx * kx + kz * kz);
				double amp = 0.0;
				// The Nyquist row and column have no mirror wave number on the grid: left empty, every field stays
				// Hermitian (real), which the packing of two fields per complex transform relies on.
				if (n > 0 && m > 0 && k > 1e-9 && k >= k_low && k < k_high) {
					double omega = Math::sqrt(WATER_GRAVITY * k);
					double domega_dk = WATER_GRAVITY / (2.0 * omega);
					double s = spectrum.jonswap(omega) * spectrum.spreading(omega, Math::atan2(kz, kx)) * domega_dk / k;
					s *= Math::exp(-k * k * 0.004 * 0.004); // capillary damping
					amp = Math::sqrt(s * dk * dk * 0.5);
				}
				uint32_t key = (uint32_t(c * p_size + m) * uint32_t(p_size) + uint32_t(n)) * 2u;
				double u1 = water_uniform(key);
				double u2 = water_uniform(key + 1u);
				double r = Math::sqrt(-2.0 * Math::log(u1));
				double a = 2.0 * Math::PI * u2;
				// Complex Gaussian with E|z|^2 = 1.
				uint32_t i = (uint32_t(m) * p_size + n) * 2;
				h0[i] = r * Math::cos(a) * Math::SQRT12 * amp;
				h0[i + 1] = r * Math::sin(a) * Math::SQRT12 * amp;
			}
		}
		for (int m = 0; m < p_size; m++) {
			for (int n = 0; n < p_size; n++) {
				uint32_t i = (uint32_t(m) * p_size + n) * 2;
				uint32_t j = (uint32_t((p_size - m) % p_size) * p_size + uint32_t((p_size - n) % p_size)) * 2;
				float *o = out + ((int64_t(c) * p_size + m) * p_size + n) * 4;
				o[0] = float(h0[i]);
				o[1] = float(h0[i + 1]);
				o[2] = float(h0[j]);
				o[3] = float(-h0[j + 1]);
			}
		}
	}
	return bytes;
}

float FellowWater3D::get_significant_wave_height() const {
	// Hs = 4 sqrt(m0) of the spectrum's cascades as configured (each mode contributes 2 |h0|^2 to the variance).
	int cascades = _cascade_count();
	Vector<uint8_t> bytes = _build_spectrum(wave_resolution, cascades);
	const float *s = reinterpret_cast<const float *>(bytes.ptr());
	double var = 0.0;
	int64_t count = int64_t(wave_resolution) * wave_resolution * cascades;
	for (int64_t i = 0; i < count; i++) {
		var += double(s[i * 4]) * s[i * 4] + double(s[i * 4 + 1]) * s[i * 4 + 1] + double(s[i * 4 + 2]) * s[i * 4 + 2] + double(s[i * 4 + 3]) * s[i * 4 + 3];
	}
	return float(4.0 * Math::sqrt(var) * wave_scale);
}

Ref<Image> FellowWater3D::debug_get_spectrum_image(int p_cascade) const {
	int cascades = _cascade_count();
	ERR_FAIL_INDEX_V(p_cascade, cascades, Ref<Image>());
	Vector<uint8_t> bytes = _build_spectrum(wave_resolution, cascades);
	int64_t layer = int64_t(wave_resolution) * wave_resolution * 16;
	Vector<uint8_t> one;
	one.resize(layer);
	memcpy(one.ptrw(), bytes.ptr() + layer * p_cascade, layer);
	return Image::create_from_data(wave_resolution, wave_resolution, false, Image::FORMAT_RGBAF, one);
}

Ref<Image> FellowWater3D::debug_get_displacement_image(int p_cascade) {
	ERR_FAIL_COND_V(displacement_texture.is_null() || !displacement_texture->get_texture_rd_rid().is_valid(), Ref<Image>());
	return displacement_texture->get_layer_data(p_cascade);
}

// ------------------------------------------------------------------ main thread

void FellowWater3D::_make_foam_noise() {
	// Tileable cellular noise (two octaves of distance to jittered points): the foam's bubbles and streaks.
	const int n = 256;
	Ref<Image> img = Image::create_empty(n, n, false, Image::FORMAT_L8);
	auto octave = [](int p_cells, uint32_t p_seed, float p_x, float p_y) {
		float cx = p_x * p_cells;
		float cy = p_y * p_cells;
		int ix = int(Math::floor(cx));
		int iy = int(Math::floor(cy));
		float best = 1e9f;
		float second = 1e9f;
		for (int oy = -1; oy <= 1; oy++) {
			for (int ox = -1; ox <= 1; ox++) {
				int gx = ix + ox;
				int gy = iy + oy;
				int wx = ((gx % p_cells) + p_cells) % p_cells;
				int wy = ((gy % p_cells) + p_cells) % p_cells;
				uint32_t key = (uint32_t(wy) * uint32_t(p_cells) + uint32_t(wx)) * 2u + p_seed;
				float px = gx + float(water_uniform(key));
				float py = gy + float(water_uniform(key + 7919u));
				float d = Math::sqrt((px - cx) * (px - cx) + (py - cy) * (py - cy));
				if (d < best) {
					second = best;
					best = d;
				} else if (d < second) {
					second = d;
				}
			}
		}
		return CLAMP(second - best, 0.0f, 1.0f);
	};
	for (int y = 0; y < n; y++) {
		for (int x = 0; x < n; x++) {
			float u = (x + 0.5f) / n;
			float v = (y + 0.5f) / n;
			float a = octave(12, 1000u, u, v);
			float b = octave(29, 5000u, u, v);
			float val = CLAMP(Math::pow(a, 0.6f) * 0.7f + b * 0.45f, 0.0f, 1.0f);
			img->set_pixel(x, y, Color(val, val, val));
		}
	}
	img->generate_mipmaps();
	foam_noise = ImageTexture::create_from_image(img);
}

void FellowWater3D::_update_mesh() {
	// Polar grid around the camera: rings growing by (1 + 2 pi / segments), so cells stay square; UV.x is the
	// vertex spacing (the band limit of the displacement).
	int segs = grid_segments;
	double q = 1.0 + 2.0 * Math::PI / segs;
	int rings = MAX(2, int(Math::ceil(Math::log(double(grid_far) / double(grid_near)) / Math::log(q))) + 1);
	PackedVector3Array verts;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedInt32Array indices;
	verts.resize(1 + rings * segs);
	normals.resize(verts.size());
	uvs.resize(verts.size());
	indices.resize((segs + (rings - 1) * segs * 2) * 3);
	Vector3 *vw = verts.ptrw();
	Vector3 *nw = normals.ptrw();
	Vector2 *uw = uvs.ptrw();
	vw[0] = Vector3();
	nw[0] = Vector3(0, 1, 0);
	uw[0] = Vector2(float(grid_near * (q - 1.0)), 0.0f);
	for (int r = 0; r < rings; r++) {
		double radius = grid_near * Math::pow(q, double(r));
		for (int s = 0; s < segs; s++) {
			double a = 2.0 * Math::PI * s / segs;
			int k = 1 + r * segs + s;
			vw[k] = Vector3(float(radius * Math::cos(a)), 0.0f, float(radius * Math::sin(a)));
			nw[k] = Vector3(0, 1, 0);
			uw[k] = Vector2(float(radius * (q - 1.0)), float(radius));
		}
	}
	int32_t *iw = indices.ptrw();
	int w = 0;
	for (int s = 0; s < segs; s++) {
		iw[w++] = 0;
		iw[w++] = 1 + (s + 1) % segs;
		iw[w++] = 1 + s;
	}
	for (int r = 0; r + 1 < rings; r++) {
		for (int s = 0; s < segs; s++) {
			int a = 1 + r * segs + s;
			int b = 1 + r * segs + (s + 1) % segs;
			int c = a + segs;
			int d = b + segs;
			iw[w++] = a;
			iw[w++] = b;
			iw[w++] = c;
			iw[w++] = b;
			iw[w++] = d;
			iw[w++] = c;
		}
	}
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = verts;
	arrays[Mesh::ARRAY_NORMAL] = normals;
	arrays[Mesh::ARRAY_TEX_UV] = uvs;
	arrays[Mesh::ARRAY_INDEX] = indices;
	Ref<ArrayMesh> mesh;
	mesh.instantiate();
	mesh->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	mesh->set_custom_aabb(AABB(Vector3(-1e6f, -1e4f, -1e6f), Vector3(2e6f, 2e4f, 2e6f)));
	if (!mesh_instance) {
		mesh_instance = memnew(MeshInstance3D);
		mesh_instance->set_name("FellowWaterSurface");
		mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		mesh_instance->set_ignore_occlusion_culling(true);
		add_child(mesh_instance, false, INTERNAL_MODE_FRONT);
	}
	mesh_instance->set_mesh(mesh);
	mesh_dirty = false;
	_update_material();
}

void FellowWater3D::_update_material() {
	// The depth texture's nearest-depth mip levels (decision 0126) are asked for only while reflections are traced:
	// without them the renderer does not build the pyramid.
	if (shader.is_null() || shader_hierarchical != screen_space_reflections) {
		if (shader.is_null()) {
			shader.instantiate();
		}
		String code = String::utf8(fellow_water_shader_code);
		if (!screen_space_reflections) {
			code = code.replace("hint_depth_texture, filter_nearest_mipmap,", "hint_depth_texture, filter_nearest,");
		}
		shader->set_code(code);
		shader_hierarchical = screen_space_reflections;
	}
	if (material.is_null()) {
		material.instantiate();
		material->set_shader(shader);
	}
	if (foam_noise.is_null()) {
		_make_foam_noise();
	}
	int cascades = _cascade_count();
	material->set_shader_parameter("wave_disp", displacement_texture);
	material->set_shader_parameter("wave_slope", slope_texture);
	material->set_shader_parameter("wave_caustics", caustics_texture);
	material->set_shader_parameter("cascade_lengths", Vector4(cascade_lengths.x, cascade_lengths.y, cascade_lengths.z, MAX(cascade_lengths.w, 1.0f)));
	// Without a RenderingDevice (the Compatibility renderer) there are no wave textures: no cascade is sampled, and
	// the surface is flat (an unbound array samples white: every vertex 1 m up and across a cascade, the water out
	// of sight; found by the web check, milestone 50).
	material->set_shader_parameter("cascade_count", RS::get_singleton()->get_rendering_device() ? cascades : 0);
	material->set_shader_parameter("wave_texels", float(wave_resolution));
	material->set_shader_parameter("wave_scale", wave_scale);
	material->set_shader_parameter("foam_noise", foam_noise);
	material->set_shader_parameter("absorption", absorption);
	material->set_shader_parameter("scattering", Vector3(scattering_color.r, scattering_color.g, scattering_color.b));
	material->set_shader_parameter("foam_color", Vector3(foam_color.r, foam_color.g, foam_color.b));
	material->set_shader_parameter("refraction_strength", refraction_strength);
	material->set_shader_parameter("base_roughness", roughness);
	material->set_shader_parameter("shore_foam_depth", shore_foam_depth);
	material->set_shader_parameter("caustics_strength", caustics_strength);
	material->set_shader_parameter("caustics_depths", caustics_depths);
	// Caustics from the cascades of at most 12 m: ripples focus sunlight within a few metres; longer waves only
	// make broad, faint patches (and blotches where their coarse texels are differentiated).
	int first = 0;
	while (first + 1 < cascades && cascade_lengths[first] > 12.0f) {
		first++;
	}
	material->set_shader_parameter("caustics_first", first);
	material->set_shader_parameter("ssr_enabled", screen_space_reflections);
	material->set_shader_parameter("ssr_steps", reflection_steps);
	material->set_shader_parameter("ssr_max_distance", reflection_distance);
	material->set_shader_parameter("ssr_min_fresnel", reflection_min_fresnel);
	material->set_shader_parameter("sim_wave_scale", simulation_wave_scale);
	material->set_shader_parameter("sim_blend", simulation_blend);
	material->set_shader_parameter("flow_period", flow_period);
	material->set_shader_parameter("debug_view", int(debug_view));
	Vector3 o = is_inside_tree() ? get_global_position() : get_position();
	if (extent.x > 0.0f && extent.y > 0.0f) {
		material->set_shader_parameter("extent", Vector4(o.x - extent.x * 0.5f, o.z - extent.y * 0.5f, o.x + extent.x * 0.5f, o.z + extent.y * 0.5f));
	} else {
		material->set_shader_parameter("extent", Vector4(-1e9f, -1e9f, 1e9f, 1e9f));
	}
	if (mesh_instance) {
		mesh_instance->set_material_override(material);
	}
}

void FellowWater3D::_update_simulation_link() {
	FellowShallowWater3D *sim = nullptr;
	if (!simulation_path.is_empty() && is_inside_tree()) {
		sim = Object::cast_to<FellowShallowWater3D>(get_node_or_null(simulation_path));
	}
	sim_id = sim ? sim->get_instance_id() : ObjectID();
	if (material.is_null()) {
		return;
	}
	material->set_shader_parameter("sim_enabled", sim != nullptr);
	if (sim) {
		material->set_shader_parameter("sim_surface", sim->get_surface_texture());
		material->set_shader_parameter("sim_flow", sim->get_flow_texture());
		material->set_shader_parameter("sim_domain", sim->get_domain_size());
	}
}

DirectionalLight3D *FellowWater3D::_find_sun() const {
	if (!sun_path.is_empty()) {
		return Object::cast_to<DirectionalLight3D>(get_node_or_null(sun_path));
	}
	if (!is_inside_tree()) {
		return nullptr;
	}
	// The first visible directional light of the scene.
	LocalVector<Node *> stack;
	stack.push_back(get_tree()->get_root());
	while (!stack.is_empty()) {
		Node *n = stack[stack.size() - 1];
		stack.resize(stack.size() - 1);
		DirectionalLight3D *l = Object::cast_to<DirectionalLight3D>(n);
		if (l && l->is_visible_in_tree() && !l->has_meta(SNAME("_fellow_lightning"))) { // a FellowLightning3D's flash is no sun
			return l;
		}
		for (int i = n->get_child_count() - 1; i >= 0; i--) {
			stack.push_back(n->get_child(i));
		}
	}
	return nullptr;
}

Vector3 FellowWater3D::_sun_direction() {
	DirectionalLight3D *sun = Object::cast_to<DirectionalLight3D>(ObjectDB::get_instance(sun_id));
	if (!sun || !sun->is_inside_tree()) {
		sun = _find_sun();
		sun_id = sun ? sun->get_instance_id() : ObjectID();
	}
	if (!sun) {
		return Vector3(0.0f, -1.0f, 0.0f);
	}
	return (-sun->get_global_transform().basis.get_column(2)).normalized();
}

void FellowWater3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_update_mesh();
			_update_simulation_link();
			set_process_internal(true);
		} break;
		case NOTIFICATION_TRANSFORM_CHANGED: {
			if (material.is_valid()) {
				_update_material();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			double dt = get_process_delta_time();
			// Still in the editor with its Animate Nodes toggle off (milestone 52): no time. Nothing written or
			// dispatched that did not change (each write had the renderer redraw).
			const bool still = fellow_editor_still();
			if (time_auto && !still) {
				time += dt;
			}
			if (mesh_dirty) {
				_update_mesh();
			}
			if (material.is_null()) {
				return;
			}
			const float water_time = float(Math::fmod(time, 3600.0));
			if (water_time != last_water_time) {
				material->set_shader_parameter("water_time", water_time);
				last_water_time = water_time;
			}
			Vector3 sun = _sun_direction();
			if (!sun.is_equal_approx(last_sun_direction)) {
				material->set_shader_parameter("sun_direction", sun);
				last_sun_direction = sun;
			}
			// FellowRain3D (decision 0124): its ripples on the water.
			FellowRain3D *rain_node = FellowRain3D::get_active();
			float rain_amount = (rain_node && rain_node->is_visible_in_tree()) ? rain_node->get_ripple_rain() * rain_node->get_ripple_strength() : 0.0f;
			if (rain_amount != last_rain_amount) {
				material->set_shader_parameter("rain_amount", rain_amount);
				last_rain_amount = rain_amount;
			}
			if (rain_node && (rain_node->get_instance_id() != last_rain_id || rain_node->get_ripple_tile() != last_rain_tile)) {
				material->set_shader_parameter("rain_ripples", rain_node->get_ripple_texture());
				material->set_shader_parameter("rain_tile", rain_node->get_ripple_tile());
				last_rain_id = rain_node->get_instance_id();
				last_rain_tile = rain_node->get_ripple_tile();
			}
			// FellowWind3D (decision 0127): its mean wind drives the spectrum (rebuilt when it changes by more than 2 % or
			// a degree), its gusts roughen the short waves where they blow and calm them in the lulls.
			FellowWind3D *wind_node = wind_from_scene ? FellowWind3D::get_active() : nullptr;
			float gust_strength = 0.0f;
			if (wind_node && wind_node->is_visible_in_tree()) {
				const Vector3 wd = wind_node->get_direction();
				const float ws = MAX(0.1f, wind_node->get_speed());
				const float wa = float(Math::rad_to_deg(Math::atan2(wd.z, wd.x)));
				const float da = Math::abs(Math::fposmod(wa - wind_direction + 180.0f, 360.0f) - 180.0f);
				if (Math::abs(ws - wind_speed) > 0.02f * MAX(ws, wind_speed) || da > 1.0f) {
					wind_speed = ws;
					wind_direction = wa;
					spectrum_dirty = true;
				}
				gust_strength = wind_node->get_gustiness() * gust_ripples;
				const Vector4 gust_map = wind_node->get_gust_mapping();
				if (gust_map != last_gust_map) {
					material->set_shader_parameter("wind_gust_map", gust_map);
					last_gust_map = gust_map;
				}
				if (wind_node->get_instance_id() != last_wind_id) {
					material->set_shader_parameter("wind_gusts", wind_node->get_gust_texture());
					last_wind_id = wind_node->get_instance_id();
				}
			}
			if (gust_strength != last_gust_strength) {
				material->set_shader_parameter("wind_gust_strength", gust_strength);
				last_gust_strength = gust_strength;
			}
			FellowShallowWater3D *sim = Object::cast_to<FellowShallowWater3D>(ObjectDB::get_instance(sim_id));
			if (sim && sim->is_inside_tree()) {
				Transform3D st = sim->get_global_transform();
				if (st != last_sim_transform) {
					material->set_shader_parameter("sim_world_to_local", Projection(st.affine_inverse()));
					material->set_shader_parameter("sim_level_offset", st.origin.y);
					last_sim_transform = st;
				}
				if (sim->get_water_mesh_instance() && sim->get_water_mesh_instance()->is_visible()) {
					sim->get_water_mesh_instance()->set_visible(false); // this node draws the simulated surface
				}
			}
			if (!Fellow::fluid_enabled() || gpu_failed) {
				return;
			}
			int cascades = _cascade_count();
			bool spectrum_sent = false;
			if (spectrum_dirty) {
				Vector<uint8_t> spectrum = _build_spectrum(wave_resolution, cascades);
				RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowWater3D::_rt_upload_spectrum).bind(spectrum));
				spectrum_dirty = false;
				spectrum_sent = true;
			}
			double step = last_dispatch_time < 0.0 ? 1.0 / 60.0 : CLAMP(time - last_dispatch_time, 0.0, 0.25);
			last_dispatch_time = time;
			PushConstant pc;
			memset(&pc, 0, sizeof(PushConstant));
			pc.size = wave_resolution;
			pc.log2_size = int(Math::round(Math::log2(double(wave_resolution))));
			pc.cascades = cascades;
			for (int i = 0; i < 4; i++) {
				pc.lengths[i] = MAX(cascade_lengths[i], 1.0f);
			}
			pc.time_fraction = float(Math::fmod(time, double(repeat_period)) / repeat_period);
			if (pc.time_fraction < 0.0f) {
				pc.time_fraction += 1.0f;
			}
			pc.repeat_period = repeat_period;
			pc.gravity = float(WATER_GRAVITY);
			pc.choppiness = choppiness;
			pc.foam_decay = float(Math::exp(-foam_decay * step));
			pc.foam_threshold = whitecap_threshold;
			pc.foam_gain = float(foam_gain * step * 60.0);
			pc.caustic_depth0 = caustics_depths.x;
			pc.sun_direction[0] = sun.x;
			pc.sun_direction[1] = MIN(sun.y, -0.05f);
			pc.sun_direction[2] = sun.z;
			pc.sun_direction[3] = caustics_depths.y;
			PackedByteArray bytes;
			bytes.resize(sizeof(PushConstant));
			memcpy(bytes.ptrw(), &pc, sizeof(PushConstant));
			if (!spectrum_sent && bytes == last_dispatch) {
				break; // the same waves as the last pass (milestone 52: a clock standing still, the editor's or a game's)
			}
			last_dispatch = bytes;
			RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowWater3D::_rt_dispatch).bind(bytes));
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (gpu_ready) {
				RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowWater3D::_rt_free));
			}
			last_dispatch_time = -1.0;
			last_dispatch.clear();
			spectrum_dirty = true;
		} break;
	}
}

// ------------------------------------------------------------------ render thread

RID FellowWater3D::_rt_uniform_set(Kernel p_kernel, const Vector<RID> &p_images) {
	Vector<RD::Uniform> u;
	for (int i = 0; i < p_images.size(); i++) {
		u.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, i, p_images[i]));
	}
	return rd->uniform_set_create(u, shaders[p_kernel], 0);
}

void FellowWater3D::_rt_create(int p_size, int p_cascades) {
	rd = RS::get_singleton()->get_rendering_device();
	if (!rd) {
		gpu_failed = true;
		WARN_PRINT("FellowWater3D needs a RenderingDevice renderer (Forward+ or Mobile): with the Compatibility renderer its surface is flat.");
		return;
	}
	if (shader_file.is_null()) {
		shader_file.instantiate();
		Error err = shader_file->parse_versions_from_text(fellow_water_waves_shader_glsl);
		if (err != OK) {
			shader_file->print_errors("fellow_water_waves");
			gpu_failed = true;
			return;
		}
	}
	for (int k = 0; k < KERNEL_MAX; k++) {
		if (shaders[k].is_null()) {
			shaders[k] = rd->shader_create_from_spirv(shader_file->get_spirv_stages(wave_kernel_names[k]), String("FellowWater ") + wave_kernel_names[k]);
			if (shaders[k].is_null()) {
				gpu_failed = true;
				ERR_PRINT("FellowWater3D: shader creation failed.");
				return;
			}
			pipelines[k] = rd->compute_pipeline_create(shaders[k]);
		}
	}
	int levels = int(Math::round(Math::log2(double(p_size)))) + 1;

	RD::TextureFormat tf;
	tf.texture_type = RD::TEXTURE_TYPE_2D_ARRAY;
	tf.width = p_size;
	tf.height = p_size;
	tf.array_layers = p_cascades;
	tf.format = RD::DATA_FORMAT_R32G32B32A32_SFLOAT;
	tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
	h0_tex = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(h0_tex, "FellowWater h0");
	tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT;
	for (int i = 0; i < 2; i++) {
		spectrum_tex[i] = rd->texture_create(tf, RD::TextureView());
		rd->set_resource_name(spectrum_tex[i], "FellowWater spectrum " + itos(i));
	}
	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.mipmaps = levels;
	tf.usage_bits = RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_FROM_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	displacement_tex = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(displacement_tex, "FellowWater displacement");
	slope_tex = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(slope_tex, "FellowWater slopes");
	caustics_tex = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(caustics_tex, "FellowWater caustics");
	// The foam accumulates in the displacement's alpha: start from zero.
	rd->texture_clear(displacement_tex, Color(0, 0, 0, 0), 0, levels, 0, p_cascades);
	RID *bases[3] = { &displacement_tex, &slope_tex, &caustics_tex };
	Vector<RID> *mips[3] = { &displacement_mips, &slope_mips, &caustics_mips };
	for (int t = 0; t < 3; t++) {
		mips[t]->clear();
		for (int l = 0; l < levels; l++) {
			mips[t]->push_back(rd->texture_create_shared_from_slice(RD::TextureView(), *bases[t], 0, l, 1, RD::TEXTURE_SLICE_2D_ARRAY, p_cascades));
		}
	}
	set_evolve = _rt_uniform_set(KERNEL_EVOLVE, { h0_tex, spectrum_tex[0], spectrum_tex[1] });
	set_fft = _rt_uniform_set(KERNEL_FFT, { spectrum_tex[0], spectrum_tex[1] });
	set_assemble = _rt_uniform_set(KERNEL_ASSEMBLE, { spectrum_tex[0], spectrum_tex[1], displacement_mips[0], slope_mips[0] });
	set_caustics = _rt_uniform_set(KERNEL_CAUSTICS, { slope_mips[0], caustics_mips[0] });
	sets_downsample.clear();
	for (int t = 0; t < 3; t++) {
		for (int l = 0; l + 1 < levels; l++) {
			sets_downsample.push_back(_rt_uniform_set(KERNEL_DOWNSAMPLE, { (*mips[t])[l], (*mips[t])[l + 1] }));
		}
	}
	allocated_size = p_size;
	allocated_cascades = p_cascades;
	allocated_levels = levels;
	gpu_ready = true;
	displacement_texture->set_texture_rd_rid(displacement_tex);
	slope_texture->set_texture_rd_rid(slope_tex);
	caustics_texture->set_texture_rd_rid(caustics_tex);
}

void FellowWater3D::_rt_free() {
	if (!rd) {
		return;
	}
	displacement_texture->set_texture_rd_rid(RID());
	slope_texture->set_texture_rd_rid(RID());
	caustics_texture->set_texture_rd_rid(RID());
	auto free_set = [this](RID &r_set) {
		if (r_set.is_valid() && rd->uniform_set_is_valid(r_set)) {
			rd->free_rid(r_set);
		}
		r_set = RID();
	};
	free_set(set_evolve);
	free_set(set_fft);
	free_set(set_assemble);
	free_set(set_caustics);
	for (RID &s : sets_downsample) {
		free_set(s);
	}
	sets_downsample.clear();
	Vector<RID> *mips[3] = { &displacement_mips, &slope_mips, &caustics_mips };
	for (int t = 0; t < 3; t++) {
		for (const RID &v : *mips[t]) {
			if (v.is_valid()) {
				rd->free_rid(v);
			}
		}
		mips[t]->clear();
	}
	RID *texs[] = { &h0_tex, &spectrum_tex[0], &spectrum_tex[1], &displacement_tex, &slope_tex, &caustics_tex };
	for (RID *r : texs) {
		if (r->is_valid()) {
			rd->free_rid(*r);
			*r = RID();
		}
	}
	gpu_ready = false;
	allocated_size = 0;
	allocated_cascades = 0;
	allocated_levels = 0;
}

void FellowWater3D::_rt_upload_spectrum(Vector<uint8_t> p_data) {
	if (gpu_failed) {
		return;
	}
	int size = wave_resolution;
	int cascades = int(p_data.size() / (int64_t(size) * size * 16));
	if (gpu_ready && (allocated_size != size || allocated_cascades != cascades)) {
		_rt_free();
	}
	if (!gpu_ready) {
		_rt_create(size, cascades);
		if (!gpu_ready) {
			return;
		}
	}
	int64_t layer = int64_t(size) * size * 16;
	for (int c = 0; c < cascades; c++) {
		Vector<uint8_t> one;
		one.resize(layer);
		memcpy(one.ptrw(), p_data.ptr() + layer * c, layer);
		rd->texture_update(h0_tex, c, one);
	}
}

void FellowWater3D::_rt_read_timestamps() {
	uint32_t n = rd->get_captured_timestamps_count();
	uint64_t t0 = 0, t1 = 0;
	int found = 0;
	for (uint32_t i = 0; i < n; i++) {
		String name = rd->get_captured_timestamp_name(i);
		if (name == "FellowWater:Begin") {
			t0 = rd->get_captured_timestamp_gpu_time(i);
			found |= 1;
		} else if (name == "FellowWater:End") {
			t1 = rd->get_captured_timestamp_gpu_time(i);
			found |= 2;
		}
	}
	if (found == 3) {
		waves_gpu_ms = double(t1 - t0) / 1e6;
	}
}

void FellowWater3D::_rt_dispatch(PackedByteArray p_push) {
	if (!gpu_ready || gpu_failed || p_push.size() != int(sizeof(PushConstant))) {
		return;
	}
	PushConstant pc;
	memcpy(&pc, p_push.ptr(), sizeof(PushConstant));
	if (pc.size != allocated_size || pc.cascades != allocated_cascades) {
		return; // the matching spectrum upload comes first
	}
	_rt_read_timestamps();
	int n = allocated_size;
	int groups = (n + 7) / 8;
	int c = allocated_cascades;
	auto run = [&](Kernel p_kernel, RID p_set, int p_x, int p_y, int p_z) {
		RD::ComputeListID list = rd->compute_list_begin();
		rd->compute_list_bind_compute_pipeline(list, pipelines[p_kernel]);
		rd->compute_list_bind_uniform_set(list, p_set, 0);
		rd->compute_list_set_push_constant(list, &pc, sizeof(PushConstant));
		rd->compute_list_dispatch(list, p_x, p_y, p_z);
		rd->compute_list_end();
	};
	rd->draw_command_begin_label("FellowEngine water waves");
	rd->capture_timestamp("FellowWater:Begin");
	run(KERNEL_EVOLVE, set_evolve, groups, groups, c);
	pc.fft_vertical = 0;
	run(KERNEL_FFT, set_fft, n, c, 2);
	pc.fft_vertical = 1;
	run(KERNEL_FFT, set_fft, n, c, 2);
	run(KERNEL_ASSEMBLE, set_assemble, groups, groups, c);
	run(KERNEL_CAUSTICS, set_caustics, groups, groups, c);
	int per_texture = allocated_levels - 1;
	for (int t = 0; t < 3; t++) {
		for (int l = 0; l < per_texture; l++) {
			pc.dst_size = MAX(n >> (l + 1), 1);
			int g = (pc.dst_size + 7) / 8;
			run(KERNEL_DOWNSAMPLE, sets_downsample[t * per_texture + l], g, g, c);
		}
	}
	rd->capture_timestamp("FellowWater:End");
	rd->draw_command_end_label();
}

// ------------------------------------------------------------------ settings

void FellowWater3D::set_wind_speed(float p_speed) {
	wind_speed = MAX(0.1f, p_speed);
	spectrum_dirty = true;
}

void FellowWater3D::set_wind_direction(float p_degrees) {
	wind_direction = p_degrees;
	spectrum_dirty = true;
}

void FellowWater3D::set_fetch(float p_km) {
	fetch = MAX(0.01f, p_km);
	spectrum_dirty = true;
}

void FellowWater3D::set_wave_scale(float p_scale) {
	wave_scale = MAX(0.0f, p_scale);
	if (material.is_valid()) {
		material->set_shader_parameter("wave_scale", wave_scale);
	}
}

void FellowWater3D::set_choppiness(float p_choppiness) {
	choppiness = MAX(0.0f, p_choppiness);
}

void FellowWater3D::set_cascade_lengths(const Vector4 &p_lengths) {
	ERR_FAIL_COND_MSG(p_lengths.x <= 0.0f, "The first cascade needs a length.");
	cascade_lengths = p_lengths;
	spectrum_dirty = true;
	if (material.is_valid()) {
		_update_material();
	}
}

void FellowWater3D::set_wave_resolution(int p_resolution) {
	ERR_FAIL_COND_MSG(p_resolution != 64 && p_resolution != 128 && p_resolution != 256 && p_resolution != 512, "Wave resolution must be 64, 128, 256 or 512.");
	wave_resolution = p_resolution;
	spectrum_dirty = true;
	if (material.is_valid()) {
		_update_material();
	}
}

void FellowWater3D::set_repeat_period(float p_seconds) {
	repeat_period = MAX(10.0f, p_seconds);
}

#define WATER_MATERIAL_SETTER(m_func, m_type, m_member) \
	void FellowWater3D::m_func(m_type p_value) { \
		m_member = p_value; \
		if (material.is_valid()) { \
			_update_material(); \
		} \
	}

WATER_MATERIAL_SETTER(set_absorption, const Vector3 &, absorption)
WATER_MATERIAL_SETTER(set_scattering_color, const Color &, scattering_color)
WATER_MATERIAL_SETTER(set_foam_color, const Color &, foam_color)
WATER_MATERIAL_SETTER(set_refraction_strength, float, refraction_strength)
WATER_MATERIAL_SETTER(set_roughness, float, roughness)
WATER_MATERIAL_SETTER(set_shore_foam_depth, float, shore_foam_depth)
WATER_MATERIAL_SETTER(set_caustics_strength, float, caustics_strength)
WATER_MATERIAL_SETTER(set_caustics_depths, const Vector2 &, caustics_depths)
WATER_MATERIAL_SETTER(set_screen_space_reflections, bool, screen_space_reflections)
WATER_MATERIAL_SETTER(set_reflection_distance, float, reflection_distance)
WATER_MATERIAL_SETTER(set_reflection_min_fresnel, float, reflection_min_fresnel)
WATER_MATERIAL_SETTER(set_simulation_wave_scale, float, simulation_wave_scale)
WATER_MATERIAL_SETTER(set_simulation_blend, float, simulation_blend)
WATER_MATERIAL_SETTER(set_flow_period, float, flow_period)
WATER_MATERIAL_SETTER(set_debug_view, DebugView, debug_view)

void FellowWater3D::set_reflection_steps(int p_steps) {
	reflection_steps = CLAMP(p_steps, 4, 128);
	if (material.is_valid()) {
		_update_material();
	}
}

void FellowWater3D::set_grid_segments(int p_segments) {
	grid_segments = CLAMP(p_segments, 16, 2048);
	mesh_dirty = true;
}

void FellowWater3D::set_grid_near(float p_near) {
	grid_near = MAX(0.01f, p_near);
	mesh_dirty = true;
}

void FellowWater3D::set_grid_far(float p_far) {
	grid_far = MAX(grid_near * 2.0f, p_far);
	mesh_dirty = true;
}

void FellowWater3D::set_extent(const Vector2 &p_extent) {
	extent = p_extent;
	if (material.is_valid()) {
		_update_material();
	}
	update_gizmos(); // milestone 52
}

void FellowWater3D::set_simulation_path(const NodePath &p_path) {
	simulation_path = p_path;
	update_configuration_warnings();
	if (is_inside_tree()) {
		_update_simulation_link();
	}
}

void FellowWater3D::set_sun_path(const NodePath &p_path) {
	sun_path = p_path;
	update_configuration_warnings();
	sun_id = ObjectID();
	last_sun_direction = Vector3();
}

PackedStringArray FellowWater3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (!Fellow::fluid_enabled()) {
		warnings.push_back(RTR("FellowEngine's fluids are off in the Project Settings (Rendering > Fellow > Enabled, or Rendering > Fellow > Fluid > Enabled): the waves are not simulated."));
	} else if (FellowWarnings::compatibility()) {
		warnings.push_back(RTR("The Compatibility renderer does not simulate the waves: the surface is flat. Use Forward+ or Mobile."));
	}
	FellowWarnings::path(warnings, this, simulation_path, "Simulation Path", "FellowShallowWater3D");
	FellowWarnings::path(warnings, this, sun_path, "Sun Path", "DirectionalLight3D");
	return warnings;
}

void FellowWater3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_wind_speed", "speed"), &FellowWater3D::set_wind_speed);
	ClassDB::bind_method(D_METHOD("get_wind_speed"), &FellowWater3D::get_wind_speed);
	ClassDB::bind_method(D_METHOD("set_wind_direction", "degrees"), &FellowWater3D::set_wind_direction);
	ClassDB::bind_method(D_METHOD("get_wind_direction"), &FellowWater3D::get_wind_direction);
	ClassDB::bind_method(D_METHOD("set_wind_from_scene", "enable"), &FellowWater3D::set_wind_from_scene);
	ClassDB::bind_method(D_METHOD("is_wind_from_scene"), &FellowWater3D::is_wind_from_scene);
	ClassDB::bind_method(D_METHOD("set_gust_ripples", "strength"), &FellowWater3D::set_gust_ripples);
	ClassDB::bind_method(D_METHOD("get_gust_ripples"), &FellowWater3D::get_gust_ripples);
	ClassDB::bind_method(D_METHOD("set_fetch", "km"), &FellowWater3D::set_fetch);
	ClassDB::bind_method(D_METHOD("get_fetch"), &FellowWater3D::get_fetch);
	ClassDB::bind_method(D_METHOD("set_wave_scale", "scale"), &FellowWater3D::set_wave_scale);
	ClassDB::bind_method(D_METHOD("get_wave_scale"), &FellowWater3D::get_wave_scale);
	ClassDB::bind_method(D_METHOD("set_choppiness", "choppiness"), &FellowWater3D::set_choppiness);
	ClassDB::bind_method(D_METHOD("get_choppiness"), &FellowWater3D::get_choppiness);
	ClassDB::bind_method(D_METHOD("set_cascade_lengths", "lengths"), &FellowWater3D::set_cascade_lengths);
	ClassDB::bind_method(D_METHOD("get_cascade_lengths"), &FellowWater3D::get_cascade_lengths);
	ClassDB::bind_method(D_METHOD("set_wave_resolution", "resolution"), &FellowWater3D::set_wave_resolution);
	ClassDB::bind_method(D_METHOD("get_wave_resolution"), &FellowWater3D::get_wave_resolution);
	ClassDB::bind_method(D_METHOD("set_repeat_period", "seconds"), &FellowWater3D::set_repeat_period);
	ClassDB::bind_method(D_METHOD("get_repeat_period"), &FellowWater3D::get_repeat_period);
	ClassDB::bind_method(D_METHOD("set_whitecap_threshold", "threshold"), &FellowWater3D::set_whitecap_threshold);
	ClassDB::bind_method(D_METHOD("get_whitecap_threshold"), &FellowWater3D::get_whitecap_threshold);
	ClassDB::bind_method(D_METHOD("set_foam_decay", "decay"), &FellowWater3D::set_foam_decay);
	ClassDB::bind_method(D_METHOD("get_foam_decay"), &FellowWater3D::get_foam_decay);
	ClassDB::bind_method(D_METHOD("set_foam_gain", "gain"), &FellowWater3D::set_foam_gain);
	ClassDB::bind_method(D_METHOD("get_foam_gain"), &FellowWater3D::get_foam_gain);
	ClassDB::bind_method(D_METHOD("set_extent", "extent"), &FellowWater3D::set_extent);
	ClassDB::bind_method(D_METHOD("get_extent"), &FellowWater3D::get_extent);
	ClassDB::bind_method(D_METHOD("set_absorption", "absorption"), &FellowWater3D::set_absorption);
	ClassDB::bind_method(D_METHOD("get_absorption"), &FellowWater3D::get_absorption);
	ClassDB::bind_method(D_METHOD("set_scattering_color", "color"), &FellowWater3D::set_scattering_color);
	ClassDB::bind_method(D_METHOD("get_scattering_color"), &FellowWater3D::get_scattering_color);
	ClassDB::bind_method(D_METHOD("set_foam_color", "color"), &FellowWater3D::set_foam_color);
	ClassDB::bind_method(D_METHOD("get_foam_color"), &FellowWater3D::get_foam_color);
	ClassDB::bind_method(D_METHOD("set_refraction_strength", "strength"), &FellowWater3D::set_refraction_strength);
	ClassDB::bind_method(D_METHOD("get_refraction_strength"), &FellowWater3D::get_refraction_strength);
	ClassDB::bind_method(D_METHOD("set_roughness", "roughness"), &FellowWater3D::set_roughness);
	ClassDB::bind_method(D_METHOD("get_roughness"), &FellowWater3D::get_roughness);
	ClassDB::bind_method(D_METHOD("set_shore_foam_depth", "depth"), &FellowWater3D::set_shore_foam_depth);
	ClassDB::bind_method(D_METHOD("get_shore_foam_depth"), &FellowWater3D::get_shore_foam_depth);
	ClassDB::bind_method(D_METHOD("set_caustics_strength", "strength"), &FellowWater3D::set_caustics_strength);
	ClassDB::bind_method(D_METHOD("get_caustics_strength"), &FellowWater3D::get_caustics_strength);
	ClassDB::bind_method(D_METHOD("set_caustics_depths", "depths"), &FellowWater3D::set_caustics_depths);
	ClassDB::bind_method(D_METHOD("get_caustics_depths"), &FellowWater3D::get_caustics_depths);
	ClassDB::bind_method(D_METHOD("set_screen_space_reflections", "enabled"), &FellowWater3D::set_screen_space_reflections);
	ClassDB::bind_method(D_METHOD("get_screen_space_reflections"), &FellowWater3D::get_screen_space_reflections);
	ClassDB::bind_method(D_METHOD("set_reflection_steps", "steps"), &FellowWater3D::set_reflection_steps);
	ClassDB::bind_method(D_METHOD("get_reflection_steps"), &FellowWater3D::get_reflection_steps);
	ClassDB::bind_method(D_METHOD("set_reflection_distance", "distance"), &FellowWater3D::set_reflection_distance);
	ClassDB::bind_method(D_METHOD("get_reflection_distance"), &FellowWater3D::get_reflection_distance);
	ClassDB::bind_method(D_METHOD("set_reflection_min_fresnel", "fresnel"), &FellowWater3D::set_reflection_min_fresnel);
	ClassDB::bind_method(D_METHOD("get_reflection_min_fresnel"), &FellowWater3D::get_reflection_min_fresnel);
	ClassDB::bind_method(D_METHOD("set_grid_segments", "segments"), &FellowWater3D::set_grid_segments);
	ClassDB::bind_method(D_METHOD("get_grid_segments"), &FellowWater3D::get_grid_segments);
	ClassDB::bind_method(D_METHOD("set_grid_near", "near"), &FellowWater3D::set_grid_near);
	ClassDB::bind_method(D_METHOD("get_grid_near"), &FellowWater3D::get_grid_near);
	ClassDB::bind_method(D_METHOD("set_grid_far", "far"), &FellowWater3D::set_grid_far);
	ClassDB::bind_method(D_METHOD("get_grid_far"), &FellowWater3D::get_grid_far);
	ClassDB::bind_method(D_METHOD("set_simulation_path", "path"), &FellowWater3D::set_simulation_path);
	ClassDB::bind_method(D_METHOD("get_simulation_path"), &FellowWater3D::get_simulation_path);
	ClassDB::bind_method(D_METHOD("set_simulation_wave_scale", "scale"), &FellowWater3D::set_simulation_wave_scale);
	ClassDB::bind_method(D_METHOD("get_simulation_wave_scale"), &FellowWater3D::get_simulation_wave_scale);
	ClassDB::bind_method(D_METHOD("set_simulation_blend", "metres"), &FellowWater3D::set_simulation_blend);
	ClassDB::bind_method(D_METHOD("get_simulation_blend"), &FellowWater3D::get_simulation_blend);
	ClassDB::bind_method(D_METHOD("set_flow_period", "seconds"), &FellowWater3D::set_flow_period);
	ClassDB::bind_method(D_METHOD("get_flow_period"), &FellowWater3D::get_flow_period);
	ClassDB::bind_method(D_METHOD("set_sun_path", "path"), &FellowWater3D::set_sun_path);
	ClassDB::bind_method(D_METHOD("get_sun_path"), &FellowWater3D::get_sun_path);
	ClassDB::bind_method(D_METHOD("set_debug_view", "view"), &FellowWater3D::set_debug_view);
	ClassDB::bind_method(D_METHOD("get_debug_view"), &FellowWater3D::get_debug_view);
	ClassDB::bind_method(D_METHOD("set_time", "seconds"), &FellowWater3D::set_time);
	ClassDB::bind_method(D_METHOD("get_time"), &FellowWater3D::get_time);
	ClassDB::bind_method(D_METHOD("set_time_auto", "enabled"), &FellowWater3D::set_time_auto);
	ClassDB::bind_method(D_METHOD("is_time_auto"), &FellowWater3D::is_time_auto);

	ClassDB::bind_method(D_METHOD("get_waves_gpu_time_ms"), &FellowWater3D::get_waves_gpu_time_ms);
	ClassDB::bind_method(D_METHOD("get_significant_wave_height"), &FellowWater3D::get_significant_wave_height);
	ClassDB::bind_method(D_METHOD("get_displacement_texture"), &FellowWater3D::get_displacement_texture);
	ClassDB::bind_method(D_METHOD("get_slope_texture"), &FellowWater3D::get_slope_texture);
	ClassDB::bind_method(D_METHOD("get_surface_mesh_instance"), &FellowWater3D::get_surface_mesh_instance);
	ClassDB::bind_method(D_METHOD("debug_get_spectrum_image", "cascade"), &FellowWater3D::debug_get_spectrum_image);
	ClassDB::bind_method(D_METHOD("debug_get_displacement_image", "cascade"), &FellowWater3D::debug_get_displacement_image);

	ADD_GROUP("Waves", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_speed", PROPERTY_HINT_RANGE, "0.1,40,0.01,suffix:m/s"), "set_wind_speed", "get_wind_speed");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_direction", PROPERTY_HINT_RANGE, "-360,360,0.1,degrees"), "set_wind_direction", "get_wind_direction");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "wind_from_scene"), "set_wind_from_scene", "is_wind_from_scene");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gust_ripples", PROPERTY_HINT_RANGE, "0,3,0.01"), "set_gust_ripples", "get_gust_ripples");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fetch", PROPERTY_HINT_RANGE, "0.01,1000,0.01,or_greater,suffix:km"), "set_fetch", "get_fetch");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wave_scale", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_wave_scale", "get_wave_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "choppiness", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_choppiness", "get_choppiness");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR4, "cascade_lengths", PROPERTY_HINT_NONE, "suffix:m"), "set_cascade_lengths", "get_cascade_lengths");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "wave_resolution", PROPERTY_HINT_ENUM, "64:64,128:128,256:256,512:512"), "set_wave_resolution", "get_wave_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "repeat_period", PROPERTY_HINT_RANGE, "10,3600,1,suffix:s"), "set_repeat_period", "get_repeat_period");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "whitecap_threshold", PROPERTY_HINT_RANGE, "-1,1,0.01"), "set_whitecap_threshold", "get_whitecap_threshold");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "foam_decay", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_foam_decay", "get_foam_decay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "foam_gain", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_foam_gain", "get_foam_gain");
	ADD_GROUP("Surface", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "extent", PROPERTY_HINT_NONE, "suffix:m"), "set_extent", "get_extent");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "absorption", PROPERTY_HINT_NONE, "suffix:1/m"), "set_absorption", "get_absorption");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "scattering_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_scattering_color", "get_scattering_color");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "foam_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_foam_color", "get_foam_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "refraction_strength", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_refraction_strength", "get_refraction_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "roughness", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_roughness", "get_roughness");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shore_foam_depth", PROPERTY_HINT_RANGE, "0,4,0.01,suffix:m"), "set_shore_foam_depth", "get_shore_foam_depth");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "caustics_strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_caustics_strength", "get_caustics_strength");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "caustics_depths", PROPERTY_HINT_NONE, "suffix:m"), "set_caustics_depths", "get_caustics_depths");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "screen_space_reflections"), "set_screen_space_reflections", "get_screen_space_reflections");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "reflection_steps", PROPERTY_HINT_RANGE, "4,128,1"), "set_reflection_steps", "get_reflection_steps");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflection_distance", PROPERTY_HINT_RANGE, "1,1000,0.1,suffix:m"), "set_reflection_distance", "get_reflection_distance");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "reflection_min_fresnel", PROPERTY_HINT_RANGE, "0,1,0.001"), "set_reflection_min_fresnel", "get_reflection_min_fresnel");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "grid_segments", PROPERTY_HINT_RANGE, "16,2048,1"), "set_grid_segments", "get_grid_segments");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grid_near", PROPERTY_HINT_RANGE, "0.01,10,0.01,suffix:m"), "set_grid_near", "get_grid_near");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "grid_far", PROPERTY_HINT_RANGE, "10,100000,1,suffix:m"), "set_grid_far", "get_grid_far");
	ADD_GROUP("Simulation", "simulation_");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "simulation_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "FellowShallowWater3D"), "set_simulation_path", "get_simulation_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "simulation_wave_scale", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_simulation_wave_scale", "get_simulation_wave_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "simulation_blend", PROPERTY_HINT_RANGE, "0,64,0.1,suffix:m"), "set_simulation_blend", "get_simulation_blend");
	ADD_GROUP("", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flow_period", PROPERTY_HINT_RANGE, "0.1,10,0.01,suffix:s"), "set_flow_period", "get_flow_period");
	ADD_PROPERTY(PropertyInfo(Variant::NODE_PATH, "sun_path", PROPERTY_HINT_NODE_PATH_VALID_TYPES, "DirectionalLight3D"), "set_sun_path", "get_sun_path");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time", PROPERTY_HINT_NONE, "suffix:s", PROPERTY_USAGE_NONE), "set_time", "get_time");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "time_auto"), "set_time_auto", "is_time_auto");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "debug_view", PROPERTY_HINT_ENUM, "Off,Reflections,Foam,Refraction,Normals,Simulation,Roughness,Reflection Cost"), "set_debug_view", "get_debug_view");

	BIND_ENUM_CONSTANT(DEBUG_VIEW_OFF);
	BIND_ENUM_CONSTANT(DEBUG_VIEW_REFLECTIONS);
	BIND_ENUM_CONSTANT(DEBUG_VIEW_FOAM);
	BIND_ENUM_CONSTANT(DEBUG_VIEW_REFRACTION);
	BIND_ENUM_CONSTANT(DEBUG_VIEW_NORMALS);
	BIND_ENUM_CONSTANT(DEBUG_VIEW_SIMULATION);
	BIND_ENUM_CONSTANT(DEBUG_VIEW_ROUGHNESS);
	BIND_ENUM_CONSTANT(DEBUG_VIEW_REFLECTION_COST);
}

FellowWater3D::FellowWater3D() {
	displacement_texture.instantiate();
	slope_texture.instantiate();
	caustics_texture.instantiate();
	set_notify_transform(true);
}

FellowWater3D::~FellowWater3D() {
	if (RS::get_singleton() && rd) {
		if (gpu_ready) {
			_rt_free();
		}
		for (int k = 0; k < KERNEL_MAX; k++) {
			if (shaders[k].is_valid()) {
				rd->free_rid(shaders[k]);
				shaders[k] = RID();
				pipelines[k] = RID();
			}
		}
	}
}
