/**************************************************************************/
/*  fellow_gltf_materials.cpp                                             */
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

// FellowEngine (decision 0390): glTF's PBR material extensions beyond the core, read into BaseMaterial3D and written
// from it, when rendering/fellow/shading/gltf_material_extensions is on:
//
// * KHR_materials_sheen: the sheen lobe (decision 0383); its colour and roughness textures into sheen_texture (RGB, A).
// * KHR_materials_clearcoat: the coat; its strength and roughness textures packed into clearcoat_texture (R, G). Godot's
//   coat roughness is linear in GGX alpha from 0.001 to 0.1 (perceptual roughness 0.03 to 0.32): rougher coats are held
//   at 0.32. The coat's own normal texture is not read (Godot's coat uses the geometric normal).
// * KHR_materials_anisotropy: the strength (matched to Godot's parameterization at the material's roughness) and the
//   direction (the rotation and the texture's directions baked into one flowmap, its strength in alpha).
// * KHR_materials_ior and KHR_materials_specular: the dielectric's reflectance at normal incidence as Godot's specular
//   (F0 = 0.16 specular^2); the specular colour's tint is averaged (Godot's dielectric reflectance is grey), its textures
//   not read.
// * KHR_materials_iridescence (milestone 250, decision 0392): the thin film (BaseMaterial3D's iridescence); its
//   strength and thickness textures packed into iridescence_texture (R, G).
// * KHR_materials_transmission and KHR_materials_volume (milestone 251, decision 0393): glass as BaseMaterial3D's
//   refraction tinted by its albedo (refraction_tint); a volume bent by its IOR and absorbing along its thickness (with
//   a thickness texture, the absorption baked into the albedo texture).
// * KHR_materials_diffuse_transmission (milestone 252): BaseMaterial3D's backlight (leaves, paper, lampshades).

#include "gltf_document.h"

#include "core/config/project_settings.h"
#include "scene/resources/image_texture.h"

namespace {

// An image's texels as RGBA8 at a given size.
struct FellowTexels {
	int width = 0;
	int height = 0;
	Vector<uint8_t> data;

	bool is_valid() const { return width > 0 && height > 0; }
	Color at(int p_index) const {
		const uint8_t *p = data.ptr() + p_index * 4;
		return Color(p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, p[3] / 255.0f);
	}
};

FellowTexels fellow_texels(const Ref<Image> &p_image, int p_width, int p_height) {
	FellowTexels t;
	if (p_image.is_null() || p_image->is_empty()) {
		return t;
	}
	Ref<Image> img = p_image->duplicate();
	if (img->is_compressed()) {
		if (img->decompress() != OK) {
			return t;
		}
	}
	img->clear_mipmaps();
	img->convert(Image::FORMAT_RGBA8);
	if (img->get_width() != p_width || img->get_height() != p_height) {
		img->resize(p_width, p_height, Image::INTERPOLATE_BILINEAR);
	}
	t.width = p_width;
	t.height = p_height;
	t.data = img->get_data();
	return t;
}

uint8_t fellow_byte(float p_value) {
	return (uint8_t)CLAMP(Math::round(p_value * 255.0f), 0.0f, 255.0f);
}

// One texture from up to two images, texel by texel (the larger's size; a missing image reads as white).
template <typename F>
Ref<ImageTexture> fellow_combine(const Ref<Image> &p_a, const Ref<Image> &p_b, const String &p_name, F p_texel) {
	int w = 0;
	int h = 0;
	for (const Ref<Image> &img : { p_a, p_b }) {
		if (img.is_valid() && !img->is_empty()) {
			w = MAX(w, img->get_width());
			h = MAX(h, img->get_height());
		}
	}
	if (w == 0 || h == 0) {
		w = 4;
		h = 4;
	}
	const FellowTexels a = fellow_texels(p_a, w, h);
	const FellowTexels b = fellow_texels(p_b, w, h);
	Vector<uint8_t> out;
	out.resize(w * h * 4);
	uint8_t *o = out.ptrw();
	const Color white(1, 1, 1, 1);
	for (int i = 0; i < w * h; i++) {
		const Color c = p_texel(a.is_valid() ? a.at(i) : white, b.is_valid() ? b.at(i) : white);
		o[i * 4 + 0] = fellow_byte(c.r);
		o[i * 4 + 1] = fellow_byte(c.g);
		o[i * 4 + 2] = fellow_byte(c.b);
		o[i * 4 + 3] = fellow_byte(c.a);
	}
	Ref<Image> img = Image::create_from_data(w, h, false, Image::FORMAT_RGBA8, out);
	img->generate_mipmaps();
	Ref<ImageTexture> tex = ImageTexture::create_from_image(img);
	tex->set_name(p_name);
	return tex;
}

// Godot's coat roughness (GGX alpha mix(0.001, 0.1, r)) from glTF's perceptual roughness (alpha r^2), and back.
float fellow_coat_roughness_from_gltf(float p_roughness) {
	return CLAMP((p_roughness * p_roughness - 0.001f) / 0.099f, 0.0f, 1.0f);
}

float fellow_coat_roughness_to_gltf(float p_roughness) {
	return Math::sqrt(0.001f + 0.099f * CLAMP(p_roughness, 0.0f, 1.0f));
}

// Godot's anisotropy a stretches GGX's alpha to alpha / sqrt(1 - 0.9 a) along the tangent and alpha sqrt(1 - 0.9 a)
// across it (a ratio of 1 / (1 - 0.9 a)); glTF's strength s raises it along the direction to mix(alpha, 1, s^2) and keeps
// it across (a ratio of mix(alpha, 1, s^2) / alpha). Matched in the ratio, at the material's roughness.
float fellow_anisotropy_from_gltf(float p_strength, float p_alpha) {
	const float s = CLAMP(p_strength, 0.0f, 1.0f);
	const float along = Math::lerp(p_alpha, 1.0f, s * s);
	return CLAMP((1.0f - p_alpha / MAX(along, 1e-6f)) / 0.9f, 0.0f, 1.0f);
}

float fellow_anisotropy_to_gltf(float p_anisotropy, float p_alpha) {
	const float a = CLAMP(p_anisotropy, 0.0f, 1.0f);
	const float ratio = 1.0f / MAX(1.0f - 0.9f * a, 1e-6f);
	if (p_alpha >= 1.0f) {
		return 0.0f;
	}
	return Math::sqrt(CLAMP(p_alpha * (ratio - 1.0f) / (1.0f - p_alpha), 0.0f, 1.0f));
}

Color fellow_linear_color(const Dictionary &p_dict, const String &p_key, const Color &p_default) {
	if (!p_dict.has(p_key)) {
		return p_default;
	}
	const Array arr = p_dict[p_key];
	if (arr.size() < 3) {
		return p_default;
	}
	return Color(arr[0], arr[1], arr[2]);
}

float fellow_float(const Dictionary &p_dict, const String &p_key, float p_default) {
	return p_dict.has(p_key) ? float(p_dict[p_key]) : p_default;
}

// The scale of the first node whose mesh uses the material (the cube root of its world basis's determinant): glTF's
// volume thickness is in the mesh's space, its attenuation distance in the world's.
float fellow_material_scale(const Dictionary &p_json, int p_material) {
	if (!p_json.has("nodes") || !p_json.has("meshes")) {
		return 1.0f;
	}
	const Array nodes = p_json["nodes"];
	const Array meshes = p_json["meshes"];
	Vector<int> parent;
	parent.resize(nodes.size());
	parent.fill(-1);
	for (int i = 0; i < nodes.size(); i++) {
		const Dictionary node = nodes[i];
		if (node.has("children")) {
			const Array children = node["children"];
			for (int k = 0; k < children.size(); k++) {
				const int c = children[k];
				if (c >= 0 && c < parent.size()) {
					parent.write[c] = i;
				}
			}
		}
	}
	auto local_det = [&](int p_node) -> float {
		const Dictionary node = nodes[p_node];
		if (node.has("matrix")) {
			const Array m = node["matrix"];
			if (m.size() == 16) {
				const Basis b(Vector3(m[0], m[1], m[2]), Vector3(m[4], m[5], m[6]), Vector3(m[8], m[9], m[10]));
				return Math::abs(b.determinant());
			}
			return 1.0f;
		}
		if (node.has("scale")) {
			const Array s = node["scale"];
			if (s.size() == 3) {
				return Math::abs(float(s[0]) * float(s[1]) * float(s[2]));
			}
		}
		return 1.0f;
	};
	for (int i = 0; i < nodes.size(); i++) {
		const Dictionary node = nodes[i];
		if (!node.has("mesh")) {
			continue;
		}
		const int mesh = node["mesh"];
		if (mesh < 0 || mesh >= meshes.size()) {
			continue;
		}
		const Dictionary mesh_dict = meshes[mesh];
		const Array primitives = mesh_dict.get("primitives", Array());
		bool uses = false;
		for (int k = 0; k < primitives.size() && !uses; k++) {
			const Dictionary prim = primitives[k];
			uses = prim.has("material") && int(prim["material"]) == p_material;
		}
		if (!uses) {
			continue;
		}
		float det = 1.0f;
		for (int n = i, guard = 0; n >= 0 && guard < nodes.size(); n = parent[n], guard++) {
			det *= local_det(n);
		}
		return det > 0.0f ? Math::pow(det, 1.0f / 3.0f) : 1.0f;
	}
	return 1.0f;
}

} // namespace

bool GLTFDocument::_fellow_material_extensions_enabled() {
	ProjectSettings *ps = ProjectSettings::get_singleton();
	const String setting = "rendering/fellow/shading/gltf_material_extensions";
	return ps && ps->has_setting(setting) && bool(ps->get_setting_with_override(setting));
}

Ref<Image> GLTFDocument::_fellow_texture_image(Ref<GLTFState> p_state, const Dictionary &p_extension, const String &p_key, int p_primary_texture_coord, int *r_texture_index) {
	if (!p_extension.has(p_key)) {
		return Ref<Image>();
	}
	const Dictionary info = p_extension[p_key];
	if (!info.has("index")) {
		return Ref<Image>();
	}
	const GLTFTextureIndex texture_index = info["index"];
	ERR_FAIL_INDEX_V(texture_index, p_state->textures.size(), Ref<Image>());
	if (r_texture_index) {
		*r_texture_index = texture_index;
	}
	const int tex_coord = info.has("texCoord") ? int(info["texCoord"]) : 0;
	if (p_primary_texture_coord != -1 && tex_coord != p_primary_texture_coord) {
		WARN_PRINT(vformat("glTF: the material's %s uses another UV map than its base color texture; Godot reads it with the base color's.", p_key));
	}
	const GLTFImageIndex image_index = p_state->textures[texture_index]->get_src_image();
	Ref<Image> image;
	if (image_index >= 0 && image_index < p_state->source_images.size()) {
		image = p_state->source_images[image_index];
	}
	if (image.is_null()) {
		const Ref<Texture2D> texture = _get_texture(p_state, texture_index, TEXTURE_TYPE_GENERIC);
		if (texture.is_valid()) {
			image = texture->get_image();
		}
	}
	return image;
}

void GLTFDocument::_fellow_parse_material_extensions(Ref<GLTFState> p_state, int p_index, const Dictionary &p_extensions, int p_primary_texture_coord, Ref<StandardMaterial3D> p_material) {
	const String name = p_material->get_name();

	if (p_extensions.has("KHR_materials_sheen")) {
		const Dictionary sheen = p_extensions["KHR_materials_sheen"];
		const Color color = fellow_linear_color(sheen, "sheenColorFactor", Color(0, 0, 0));
		int color_index = -1;
		int roughness_index = -1;
		const Ref<Image> color_image = _fellow_texture_image(p_state, sheen, "sheenColorTexture", p_primary_texture_coord, &color_index);
		const Ref<Image> roughness_image = _fellow_texture_image(p_state, sheen, "sheenRoughnessTexture", p_primary_texture_coord, &roughness_index);
		if (color.r > 0.0f || color.g > 0.0f || color.b > 0.0f) {
			p_material->set_sheen_enabled(true);
			p_material->set_sheen_color(color.linear_to_srgb());
			p_material->set_sheen_roughness(fellow_float(sheen, "sheenRoughnessFactor", 0.0f));
			if (color_image.is_valid() && color_index == roughness_index) {
				// (one texture: its colour in RGB, its roughness in A, as BaseMaterial3D reads it)
				p_material->set_sheen_texture(_get_texture(p_state, color_index, TEXTURE_TYPE_GENERIC));
			} else if (color_image.is_valid() || roughness_image.is_valid()) {
				p_material->set_sheen_texture(fellow_combine(color_image, roughness_image, name + "_sheen", [](const Color &p_c, const Color &p_r) {
					return Color(p_c.r, p_c.g, p_c.b, p_r.a);
				}));
			}
		}
	}

	if (p_extensions.has("KHR_materials_clearcoat")) {
		const Dictionary coat = p_extensions["KHR_materials_clearcoat"];
		const float strength = fellow_float(coat, "clearcoatFactor", 0.0f);
		const float roughness = fellow_float(coat, "clearcoatRoughnessFactor", 0.0f);
		if (strength > 0.0f) {
			p_material->set_feature(BaseMaterial3D::FEATURE_CLEARCOAT, true);
			p_material->set_clearcoat(strength);
			int strength_index = -1;
			int roughness_index = -1;
			const Ref<Image> strength_image = _fellow_texture_image(p_state, coat, "clearcoatTexture", p_primary_texture_coord, &strength_index);
			const Ref<Image> roughness_image = _fellow_texture_image(p_state, coat, "clearcoatRoughnessTexture", p_primary_texture_coord, &roughness_index);
			if (roughness_image.is_valid()) {
				// (the roughness is not linear between the two: baked with its factor, Godot's factor 1)
				p_material->set_clearcoat_roughness(1.0f);
				p_material->set_texture(BaseMaterial3D::TEXTURE_CLEARCOAT, fellow_combine(strength_image, roughness_image, name + "_clearcoat", [roughness](const Color &p_s, const Color &p_r) {
					return Color(p_s.r, fellow_coat_roughness_from_gltf(roughness * p_r.g), 0.0f, 1.0f);
				}));
			} else {
				p_material->set_clearcoat_roughness(fellow_coat_roughness_from_gltf(roughness));
				if (strength_image.is_valid()) {
					p_material->set_texture(BaseMaterial3D::TEXTURE_CLEARCOAT, fellow_combine(strength_image, Ref<Image>(), name + "_clearcoat", [](const Color &p_s, const Color &p_r) {
						return Color(p_s.r, 1.0f, 0.0f, 1.0f);
					}));
				}
			}
			if (roughness > 0.32f) {
				print_verbose(vformat("glTF: material %s's clear coat roughness %.2f is held at Godot's 0.32.", name, roughness));
			}
			if (coat.has("clearcoatNormalTexture")) {
				print_verbose(vformat("glTF: material %s's clear coat normal texture is not read (Godot's coat follows the geometric normal).", name));
			}
		}
	}

	if (p_extensions.has("KHR_materials_anisotropy")) {
		const Dictionary aniso = p_extensions["KHR_materials_anisotropy"];
		const float strength = fellow_float(aniso, "anisotropyStrength", 0.0f);
		const float rotation = fellow_float(aniso, "anisotropyRotation", 0.0f);
		const Ref<Image> direction_image = _fellow_texture_image(p_state, aniso, "anisotropyTexture", p_primary_texture_coord, nullptr);
		if (strength > 0.0f) {
			const float alpha = MAX(p_material->get_roughness() * p_material->get_roughness(), 1e-3f);
			const float full = fellow_anisotropy_from_gltf(strength, alpha);
			p_material->set_feature(BaseMaterial3D::FEATURE_ANISOTROPY, true);
			p_material->set_anisotropy(full);
			if (direction_image.is_valid() || !Math::is_zero_approx(rotation)) {
				const float cr = Math::cos(rotation);
				const float sr = Math::sin(rotation);
				const bool has_texture = direction_image.is_valid();
				p_material->set_texture(BaseMaterial3D::TEXTURE_FLOWMAP, fellow_combine(direction_image, Ref<Image>(), name + "_anisotropy", [=](const Color &p_d, const Color &p_unused) {
					Vector2 d = has_texture ? Vector2(p_d.r * 2.0f - 1.0f, p_d.g * 2.0f - 1.0f) : Vector2(1.0f, 0.0f);
					d = Vector2(d.x * cr - d.y * sr, d.x * sr + d.y * cr);
					d = d.length() > 1e-4f ? d.normalized() : Vector2(cr, sr);
					const float here = has_texture ? fellow_anisotropy_from_gltf(strength * p_d.b, alpha) / MAX(full, 1e-6f) : 1.0f;
					// (BaseMaterial3D reads the flowmap's direction from RG, its strength from A)
					return Color(d.x * 0.5f + 0.5f, d.y * 0.5f + 0.5f, 1.0f, here);
				}));
			}
		}
	}

	if (p_extensions.has("KHR_materials_iridescence")) {
		// (milestone 250, decision 0392)
		const Dictionary irid = p_extensions["KHR_materials_iridescence"];
		const float strength = fellow_float(irid, "iridescenceFactor", 0.0f);
		if (strength > 0.0f) {
			p_material->set_iridescence_enabled(true);
			p_material->set_iridescence_strength(strength);
			p_material->set_iridescence_ior(fellow_float(irid, "iridescenceIor", 1.3f));
			p_material->set_iridescence_thickness_min(fellow_float(irid, "iridescenceThicknessMinimum", 100.0f));
			p_material->set_iridescence_thickness_max(fellow_float(irid, "iridescenceThicknessMaximum", 400.0f));
			int strength_index = -1;
			int thickness_index = -1;
			const Ref<Image> strength_image = _fellow_texture_image(p_state, irid, "iridescenceTexture", p_primary_texture_coord, &strength_index);
			const Ref<Image> thickness_image = _fellow_texture_image(p_state, irid, "iridescenceThicknessTexture", p_primary_texture_coord, &thickness_index);
			if (strength_image.is_valid() && strength_index == thickness_index) {
				// (one texture: R the strength, G the thickness, as BaseMaterial3D reads it)
				p_material->set_iridescence_texture(_get_texture(p_state, strength_index, TEXTURE_TYPE_GENERIC));
			} else if (strength_image.is_valid() || thickness_image.is_valid()) {
				const bool has_thickness = thickness_image.is_valid();
				p_material->set_iridescence_texture(fellow_combine(strength_image, thickness_image, name + "_iridescence", [has_thickness](const Color &p_s, const Color &p_t) {
					// (without a thickness texture glTF's thickness is its maximum: G 1)
					return Color(p_s.r, has_thickness ? p_t.g : 1.0f, 0.0f, 1.0f);
				}));
			}
		}
	}

	if (p_extensions.has("KHR_materials_transmission")) {
		// (milestone 251, decision 0393) Glass: the background seen through the surface (BaseMaterial3D's refraction:
		// the screen behind, blurred by the roughness), tinted by the base colour; thin-walled straight through, a volume
		// bent by its IOR (Godot's screen-space offset) and absorbing along its thickness (Beer-Lambert).
		const Dictionary trans = p_extensions["KHR_materials_transmission"];
		const float factor = CLAMP(fellow_float(trans, "transmissionFactor", 0.0f), 0.0f, 1.0f);
		if (factor > 0.0f) {
			Dictionary volume;
			if (p_extensions.has("KHR_materials_volume")) {
				volume = p_extensions["KHR_materials_volume"];
			}
			float thickness = MAX(fellow_float(volume, "thicknessFactor", 0.0f), 0.0f);
			if (thickness > 0.0f) {
				thickness *= fellow_material_scale(p_state->json, p_index); // (the mesh's space to the world's)
			}
			const float ior = p_extensions.has("KHR_materials_ior") ? fellow_float(p_extensions["KHR_materials_ior"], "ior", 1.5f) : 1.5f;
			p_material->set_feature(BaseMaterial3D::FEATURE_REFRACTION, true);
			p_material->set_refraction_tint(true);
			p_material->set_refraction(thickness > 0.0f ? 0.05f * CLAMP((ior - 1.0f) / 0.5f, 0.0f, 2.0f) : 0.0f);
			Color tint = p_material->get_albedo();
			const float distance = fellow_float(volume, "attenuationDistance", 0.0f);
			const Color absorbed = fellow_linear_color(volume, "attenuationColor", Color(1, 1, 1));
			const Ref<Image> thickness_image = _fellow_texture_image(p_state, volume, "thicknessTexture", p_primary_texture_coord, nullptr);
			const bool absorbs = thickness > 0.0f && distance > 0.0f;
			if (absorbs && thickness_image.is_null()) {
				Color lin = tint.srgb_to_linear();
				const float path = thickness / distance;
				lin.r *= Math::pow(CLAMP(absorbed.r, 0.0f, 1.0f), path);
				lin.g *= Math::pow(CLAMP(absorbed.g, 0.0f, 1.0f), path);
				lin.b *= Math::pow(CLAMP(absorbed.b, 0.0f, 1.0f), path);
				const float a = tint.a;
				tint = lin.linear_to_srgb();
				tint.a = a;
			}
			// (BaseMaterial3D's refraction lets through 1 - the albedo's alpha)
			const Ref<Image> trans_image = _fellow_texture_image(p_state, trans, "transmissionTexture", p_primary_texture_coord, nullptr);
			if (trans_image.is_valid()) {
				const Ref<Texture2D> albedo_tex = p_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
				const Ref<Image> albedo_image = albedo_tex.is_valid() ? albedo_tex->get_image() : Ref<Image>();
				p_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, fellow_combine(albedo_image, trans_image, name + "_albedo_transmission", [factor](const Color &p_a, const Color &p_t) {
					return Color(p_a.r, p_a.g, p_a.b, 1.0f - factor * p_t.r);
				}));
				tint.a = 1.0f;
			} else {
				tint.a = 1.0f - factor;
			}
			p_material->set_albedo(tint);
			if (absorbs && thickness_image.is_valid()) {
				// (the absorption along the texture's thickness — its G times the factor — baked into the albedo
				// texture's colour: thin parts lighter, thick ones deeper)
				const Ref<Texture2D> albedo_tex = p_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO);
				const Ref<Image> albedo_image = albedo_tex.is_valid() ? albedo_tex->get_image() : Ref<Image>();
				const Color ab(CLAMP(absorbed.r, 0.0f, 1.0f), CLAMP(absorbed.g, 0.0f, 1.0f), CLAMP(absorbed.b, 0.0f, 1.0f));
				p_material->set_texture(BaseMaterial3D::TEXTURE_ALBEDO, fellow_combine(albedo_image, thickness_image, name + "_albedo_volume", [thickness, distance, ab](const Color &p_a, const Color &p_t) {
					const float path = thickness * p_t.g / distance;
					Color lin = Color(p_a.r, p_a.g, p_a.b).srgb_to_linear();
					lin.r *= Math::pow(ab.r, path);
					lin.g *= Math::pow(ab.g, path);
					lin.b *= Math::pow(ab.b, path);
					Color out = lin.linear_to_srgb();
					out.a = p_a.a;
					return out;
				}));
			}
		}
	}

	if (p_extensions.has("KHR_materials_diffuse_transmission")) {
		// (milestone 252, decision 0393) Light through a thin diffusing surface — leaves, paper, a lampshade, thin porcelain:
		// BaseMaterial3D's backlight (the light from behind, scattered), the transmission's factor times its colour; their
		// textures baked into the backlight texture. BaseMaterial3D multiplies the backlight by the albedo (glTF does not):
		// undone for a plain albedo colour, kept where the albedo has a texture.
		const Dictionary dt = p_extensions["KHR_materials_diffuse_transmission"];
		const float factor = CLAMP(fellow_float(dt, "diffuseTransmissionFactor", 0.0f), 0.0f, 1.0f);
		if (factor > 0.0f) {
			const Color colour = fellow_linear_color(dt, "diffuseTransmissionColorFactor", Color(1, 1, 1));
			const Ref<Image> factor_image = _fellow_texture_image(p_state, dt, "diffuseTransmissionTexture", p_primary_texture_coord, nullptr);
			const Ref<Image> colour_image = _fellow_texture_image(p_state, dt, "diffuseTransmissionColorTexture", p_primary_texture_coord, nullptr);
			p_material->set_feature(BaseMaterial3D::FEATURE_BACKLIGHT, true);
			if (factor_image.is_valid() || colour_image.is_valid()) {
				// (BaseMaterial3D adds its backlight colour and texture: the colour black, the texture the product)
				p_material->set_backlight(Color(0, 0, 0));
				const Color scale = colour * factor;
				p_material->set_texture(BaseMaterial3D::TEXTURE_BACKLIGHT, fellow_combine(colour_image, factor_image, name + "_backlight", [scale](const Color &p_c, const Color &p_f) {
					// (the colour texture is sRGB; BaseMaterial3D reads its backlight texture as linear values)
					Color lin = Color(p_c.r, p_c.g, p_c.b).srgb_to_linear() * Color(scale.r, scale.g, scale.b) * p_f.a;
					lin.a = 1.0f;
					return lin;
				}));
			} else {
				// (glTF's transmitted light is its colour alone; BaseMaterial3D multiplies the backlight by the albedo:
				// divided by a plain albedo colour here, where it has no texture)
				Color c = colour * factor;
				if (p_material->get_texture(BaseMaterial3D::TEXTURE_ALBEDO).is_null()) {
					const Color albedo = p_material->get_albedo().srgb_to_linear();
					c.r = MIN(c.r / MAX(albedo.r, 0.02f), 16.0f);
					c.g = MIN(c.g / MAX(albedo.g, 0.02f), 16.0f);
					c.b = MIN(c.b / MAX(albedo.b, 0.02f), 16.0f);
				}
				c.a = 1.0f;
				p_material->set_backlight(c.linear_to_srgb());
			}
		}
	}

	if (p_extensions.has("KHR_materials_ior") || p_extensions.has("KHR_materials_specular")) {
		float ior = 1.5f;
		if (p_extensions.has("KHR_materials_ior")) {
			ior = fellow_float(p_extensions["KHR_materials_ior"], "ior", 1.5f);
		}
		float f0 = ior <= 0.0f ? 1.0f : Math::pow((ior - 1.0f) / (ior + 1.0f), 2.0f);
		if (p_extensions.has("KHR_materials_specular")) {
			const Dictionary spec = p_extensions["KHR_materials_specular"];
			const Color tint = fellow_linear_color(spec, "specularColorFactor", Color(1, 1, 1));
			f0 = MIN(f0 * (tint.r + tint.g + tint.b) / 3.0f, 1.0f) * fellow_float(spec, "specularFactor", 1.0f);
			if (spec.has("specularTexture") || spec.has("specularColorTexture")) {
				print_verbose(vformat("glTF: material %s's specular textures are not read (Godot's specular is one value).", name));
			}
		}
		p_material->set_specular(CLAMP(Math::sqrt(MAX(f0, 0.0f) / 0.16f), 0.0f, 1.0f));
	}
}

void GLTFDocument::_fellow_serialize_material_extensions(Ref<GLTFState> p_state, Ref<BaseMaterial3D> p_material, const String &p_material_name, Dictionary &r_extensions) {
	const bool textures = _image_format != "None";
	const StandardMaterial3D::TextureFilter filter = p_material->get_texture_filter();
	const bool repeat = p_material->get_flag(BaseMaterial3D::FLAG_USE_TEXTURE_REPEAT);
	auto texture_info = [&](const Ref<Texture2D> &p_texture) {
		Dictionary info;
		if (textures && p_texture.is_valid() && p_texture->get_image().is_valid()) {
			const GLTFTextureIndex index = _set_texture(p_state, p_texture, filter, repeat);
			if (index != -1) {
				info["index"] = index;
			}
		}
		return info;
	};

	if (p_material->is_sheen_enabled()) {
		Dictionary sheen;
		const Color c = p_material->get_sheen_color().srgb_to_linear();
		sheen["sheenColorFactor"] = Array({ c.r, c.g, c.b });
		sheen["sheenRoughnessFactor"] = p_material->get_sheen_roughness();
		const Dictionary info = texture_info(p_material->get_sheen_texture());
		if (!info.is_empty()) {
			sheen["sheenColorTexture"] = info;
			sheen["sheenRoughnessTexture"] = info;
		}
		r_extensions["KHR_materials_sheen"] = sheen;
		p_state->add_used_extension("KHR_materials_sheen");
	}

	if (p_material->get_feature(BaseMaterial3D::FEATURE_CLEARCOAT) && p_material->get_clearcoat() > 0.0f) {
		Dictionary coat;
		coat["clearcoatFactor"] = p_material->get_clearcoat();
		const float roughness = p_material->get_clearcoat_roughness();
		const float gltf_roughness = fellow_coat_roughness_to_gltf(roughness);
		coat["clearcoatRoughnessFactor"] = gltf_roughness;
		const Ref<Texture2D> tex = p_material->get_texture(BaseMaterial3D::TEXTURE_CLEARCOAT);
		if (textures && tex.is_valid() && tex->get_image().is_valid()) {
			const Ref<ImageTexture> baked = fellow_combine(tex->get_image(), Ref<Image>(), p_material_name + "_clearcoat", [roughness, gltf_roughness](const Color &p_t, const Color &p_unused) {
				return Color(p_t.r, fellow_coat_roughness_to_gltf(roughness * p_t.g) / MAX(gltf_roughness, 1e-6f), 0.0f, 1.0f);
			});
			const Dictionary info = texture_info(baked);
			if (!info.is_empty()) {
				coat["clearcoatTexture"] = info;
				coat["clearcoatRoughnessTexture"] = info;
			}
		}
		r_extensions["KHR_materials_clearcoat"] = coat;
		p_state->add_used_extension("KHR_materials_clearcoat");
	}

	if (p_material->get_feature(BaseMaterial3D::FEATURE_ANISOTROPY) && p_material->get_anisotropy() > 0.0f) {
		Dictionary aniso;
		const float alpha = MAX(p_material->get_roughness() * p_material->get_roughness(), 1e-3f);
		const float anisotropy = p_material->get_anisotropy();
		const float strength = fellow_anisotropy_to_gltf(anisotropy, alpha);
		aniso["anisotropyStrength"] = strength;
		const Ref<Texture2D> tex = p_material->get_texture(BaseMaterial3D::TEXTURE_FLOWMAP);
		if (textures && tex.is_valid() && tex->get_image().is_valid()) {
			const Ref<ImageTexture> baked = fellow_combine(tex->get_image(), Ref<Image>(), p_material_name + "_anisotropy", [=](const Color &p_t, const Color &p_unused) {
				return Color(p_t.r, p_t.g, fellow_anisotropy_to_gltf(anisotropy * p_t.a, alpha) / MAX(strength, 1e-6f), 1.0f);
			});
			const Dictionary info = texture_info(baked);
			if (!info.is_empty()) {
				aniso["anisotropyTexture"] = info;
			}
		}
		r_extensions["KHR_materials_anisotropy"] = aniso;
		p_state->add_used_extension("KHR_materials_anisotropy");
	}

	if (p_material->is_iridescence_enabled() && p_material->get_iridescence_strength() > 0.0f) {
		Dictionary irid;
		irid["iridescenceFactor"] = p_material->get_iridescence_strength();
		irid["iridescenceIor"] = p_material->get_iridescence_ior();
		irid["iridescenceThicknessMinimum"] = p_material->get_iridescence_thickness_min();
		irid["iridescenceThicknessMaximum"] = p_material->get_iridescence_thickness_max();
		const Dictionary info = texture_info(p_material->get_iridescence_texture());
		if (!info.is_empty()) {
			irid["iridescenceTexture"] = info;
			irid["iridescenceThicknessTexture"] = info;
		}
		r_extensions["KHR_materials_iridescence"] = irid;
		p_state->add_used_extension("KHR_materials_iridescence");
	}

	if (p_material->get_feature(BaseMaterial3D::FEATURE_REFRACTION) && p_material->is_refraction_tint()) {
		// (thin-walled: the refraction's bend is not written)
		Dictionary trans;
		trans["transmissionFactor"] = CLAMP(1.0f - p_material->get_albedo().a, 0.0f, 1.0f);
		r_extensions["KHR_materials_transmission"] = trans;
		p_state->add_used_extension("KHR_materials_transmission");
	}

	if (!Math::is_equal_approx(p_material->get_specular(), 0.5f)) {
		// (F0 = 0.16 specular^2 = ((ior - 1) / (ior + 1))^2)
		const float root = Math::sqrt(0.16f) * CLAMP(p_material->get_specular(), 0.0f, 1.0f);
		Dictionary ior;
		ior["ior"] = (1.0f + root) / (1.0f - root);
		r_extensions["KHR_materials_ior"] = ior;
		p_state->add_used_extension("KHR_materials_ior");
	}
}
