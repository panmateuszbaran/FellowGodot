/**************************************************************************/
/*  fellow_rainbow.cpp                                                    */
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

#include "fellow_rainbow.h"

#include "servers/rendering/renderer_rd/storage_rd/material_storage.h"
#include "servers/rendering/renderer_rd/uniform_set_cache_rd.h"

using namespace RendererRD;

// CIE 1931 colour matching functions, the multi-lobe fit of Wyman, Sloan and Shirley (2013).
static double _fellow_lobe(double p_l, double p_mu, double p_s1, double p_s2) {
	const double t = (p_l - p_mu) / (p_l < p_mu ? p_s1 : p_s2);
	return Math::exp(-0.5 * t * t);
}

// The bows by angle from the point opposite the sun, for sunlight (flat over the visible spectrum): for each wavelength,
// water's index of refraction (n = 1.3240 + 3046 / λ², λ in nm: 1.343 at 400 nm, 1.330 at 700 nm) gives Descartes'
// angles — the primary bow's 4r - 2i at the incidence of least deviation (cos² i = (n² - 1) / 3), the secondary's
// π + 2i - 6r (cos² i = (n² - 1) / 8). Geometric optics brightens towards each bow's angle as 1 / sqrt of the distance from
// it, on its inner side for the primary (the sky inside it brighter, nothing just outside: Alexander's dark band) and its
// outer side for the secondary (0.43 of the primary's light); softened by the sun's disc and the drops' diffraction
// (0.35° and 0.5°). Summed through the colour matching functions to linear sRGB, desaturated to the gamut, the primary's
// brightest luminance 1.
void FellowRainbow::_build_table() {
	Vector<double> xyz;
	xyz.resize(TABLE_SIZE * 3);
	xyz.fill(0.0);
	double *t = xyz.ptrw();
	for (int l = 380; l <= 720; l += 4) {
		const double lambda = double(l);
		const double n = 1.3240 + 3046.0 / (lambda * lambda);
		const double i1 = Math::acos(Math::sqrt((n * n - 1.0) / 3.0));
		const double r1 = Math::asin(Math::sin(i1) / n);
		const double bow1 = Math::rad_to_deg(4.0 * r1 - 2.0 * i1);
		const double i2 = Math::acos(Math::sqrt((n * n - 1.0) / 8.0));
		const double r2 = Math::asin(Math::sin(i2) / n);
		const double bow2 = Math::rad_to_deg(Math::PI + 2.0 * i2 - 6.0 * r2);
		const double X = 1.056 * _fellow_lobe(lambda, 599.8, 37.9, 31.0) + 0.362 * _fellow_lobe(lambda, 442.0, 16.0, 26.7) - 0.065 * _fellow_lobe(lambda, 501.1, 20.4, 26.2);
		const double Y = 0.821 * _fellow_lobe(lambda, 568.8, 46.9, 40.5) + 0.286 * _fellow_lobe(lambda, 530.9, 16.3, 31.1);
		const double Z = 1.217 * _fellow_lobe(lambda, 437.0, 11.8, 36.0) + 0.681 * _fellow_lobe(lambda, 459.0, 26.0, 13.8);
		for (int k = 0; k < TABLE_SIZE; k++) {
			const double theta = TABLE_MIN + (double(k) + 0.5) / double(TABLE_SIZE) * (TABLE_MAX - TABLE_MIN);
			// Primary: inside the bow (theta < bow1).
			const double x1 = bow1 - theta;
			const double w1 = 0.35;
			const double e1 = CLAMP((x1 + w1) / (2.0 * w1), 0.0, 1.0);
			double p = e1 * e1 * (3.0 - 2.0 * e1) / Math::sqrt(MAX(x1, 0.0) + w1) * Math::exp(-MAX(x1, 0.0) / 4.0);
			// Secondary: outside it (theta > bow2).
			const double x2 = theta - bow2;
			const double w2 = 0.5;
			const double e2 = CLAMP((x2 + w2) / (2.0 * w2), 0.0, 1.0);
			p += 0.43 * e2 * e2 * (3.0 - 2.0 * e2) / Math::sqrt(MAX(x2, 0.0) + w2) * Math::exp(-MAX(x2, 0.0) / 4.0);
			t[k * 3 + 0] += p * X;
			t[k * 3 + 1] += p * Y;
			t[k * 3 + 2] += p * Z;
		}
	}
	double peak = 0.0;
	for (int k = 0; k < TABLE_SIZE; k++) {
		peak = MAX(peak, t[k * 3 + 1]);
	}
	Vector<uint8_t> data;
	data.resize(TABLE_SIZE * 4 * sizeof(uint16_t));
	uint16_t *d = (uint16_t *)data.ptrw();
	for (int k = 0; k < TABLE_SIZE; k++) {
		const double X = t[k * 3 + 0] / peak, Y = t[k * 3 + 1] / peak, Z = t[k * 3 + 2] / peak;
		double rgb[3] = {
			3.2406 * X - 1.5372 * Y - 0.4986 * Z,
			-0.9689 * X + 1.8758 * Y + 0.0415 * Z,
			0.0557 * X - 0.2040 * Y + 1.0570 * Z,
		};
		// Into the gamut: towards the grey of the same luminance until no channel is negative.
		double s = 0.0;
		for (int c = 0; c < 3; c++) {
			if (rgb[c] < 0.0) {
				s = MAX(s, -rgb[c] / MAX(Y - rgb[c], 1e-9));
			}
		}
		for (int c = 0; c < 3; c++) {
			d[k * 4 + c] = Math::make_half_float(float(MAX(0.0, rgb[c] + (Y - rgb[c]) * s)));
		}
		d[k * 4 + 3] = Math::make_half_float(1.0f);
	}
	RD::TextureFormat tf;
	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.width = TABLE_SIZE;
	tf.height = 1;
	tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT;
	Vector<Vector<uint8_t>> layers;
	layers.push_back(data);
	table = RD::get_singleton()->texture_create(tf, RD::TextureView(), layers);
}

FellowRainbow::FellowRainbow() {
	Vector<String> modes;
	modes.push_back("\n");
	shader.initialize(modes);
	shader_version = shader.version_create();
	pipeline = RD::get_singleton()->compute_pipeline_create(shader.version_get_shader(shader_version, 0));
	Vector<uint8_t> zeros;
	zeros.resize(sizeof(UBO));
	zeros.fill(0);
	ubo = RD::get_singleton()->uniform_buffer_create(sizeof(UBO), zeros);
	_build_table();
	RD::TextureFormat tf;
	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.width = 1;
	tf.height = 1;
	tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_CAN_COPY_TO_BIT;
	white = RD::get_singleton()->texture_create(tf, RD::TextureView());
	RD::get_singleton()->texture_clear(white, Color(1, 1, 1, 1), 0, 1, 0, 1);
}

FellowRainbow::~FellowRainbow() {
	RID rids[] = { ubo, table, white };
	for (RID r : rids) {
		if (r.is_valid()) {
			RD::get_singleton()->free_rid(r);
		}
	}
	shader.version_free(shader_version);
}

void FellowRainbow::process(const Params &p_params) {
	UniformSetCacheRD *uniform_set_cache = UniformSetCacheRD::get_singleton();
	ERR_FAIL_NULL(uniform_set_cache);
	MaterialStorage *material_storage = MaterialStorage::get_singleton();
	ERR_FAIL_NULL(material_storage);
	RID linear = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_LINEAR, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);
	RID nearest = material_storage->sampler_rd_get_default(RSE::CANVAS_ITEM_TEXTURE_FILTER_NEAREST, RSE::CANVAS_ITEM_TEXTURE_REPEAT_DISABLED);

	UBO u;
	memset(&u, 0, sizeof(UBO));
	const Projection inv = p_params.view_projection.inverse();
	for (int i = 0; i < 4; i++) {
		for (int j = 0; j < 4; j++) {
			u.inv_view_projection[i * 4 + j] = inv.columns[i][j];
		}
	}
	u.camera_position[0] = p_params.camera_position.x;
	u.camera_position[1] = p_params.camera_position.y;
	u.camera_position[2] = p_params.camera_position.z;
	u.camera_position[3] = 1.0f / MAX(p_params.distance, 1.0f) * 0.6931472f; // half at `distance`
	const Vector3 anti = -p_params.sun_direction.normalized();
	u.anti_sun[0] = anti.x;
	u.anti_sun[1] = anti.y;
	u.anti_sun[2] = anti.z;
	u.light[0] = p_params.light.r;
	u.light[1] = p_params.light.g;
	u.light[2] = p_params.light.b;
	const bool shadow = p_params.cloud_shadow.is_valid() && p_params.cloud_shadow_strength > 0.0f;
	if (shadow) {
		u.cloud_shadow[0] = p_params.cloud_shadow_origin.x;
		u.cloud_shadow[1] = p_params.cloud_shadow_origin.y;
		u.cloud_shadow[2] = p_params.cloud_shadow_inv_extent;
		u.cloud_shadow[3] = p_params.cloud_shadow_strength;
		u.cloud_shadow2[0] = p_params.cloud_shadow_slope.x;
		u.cloud_shadow2[1] = p_params.cloud_shadow_slope.y;
	}
	u.size[0] = p_params.size.x;
	u.size[1] = p_params.size.y;
	RD::get_singleton()->buffer_update(ubo, 0, sizeof(UBO), &u);

	RD::get_singleton()->draw_command_begin_label("FellowEngine Rainbow");
	RID shader_rd = shader.version_get_shader(shader_version, 0);
	RD::ComputeListID list = RD::get_singleton()->compute_list_begin();
	RD::get_singleton()->compute_list_bind_compute_pipeline(list, pipeline);
	RD::Uniform u_params(RD::UNIFORM_TYPE_UNIFORM_BUFFER, 0, ubo);
	RD::Uniform u_depth(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 1, Vector<RID>({ nearest, p_params.depth }));
	RD::Uniform u_table(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 2, Vector<RID>({ linear, table }));
	RD::Uniform u_shadow(RD::UNIFORM_TYPE_SAMPLER_WITH_TEXTURE, 3, Vector<RID>({ linear, shadow ? p_params.cloud_shadow : white }));
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 0, u_params, u_depth, u_table, u_shadow), 0);
	RD::Uniform u_dest(RD::UNIFORM_TYPE_IMAGE, 0, p_params.color);
	RD::get_singleton()->compute_list_bind_uniform_set(list, uniform_set_cache->get_cache(shader_rd, 1, u_dest), 1);
	RD::get_singleton()->compute_list_dispatch_threads(list, p_params.size.x, p_params.size.y, 1);
	RD::get_singleton()->compute_list_end();
	RD::get_singleton()->draw_command_end_label();
}
