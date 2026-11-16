/**************************************************************************/
/*  fellow_hair_3d.h                                                      */
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

#include "core/templates/local_vector.h"
#include "scene/3d/visual_instance_3d.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/multimesh.h"

// FellowEngine (milestone 124, decision 0248): hair of strands — a head's guide strands simulated on the CPU (positions
// with the root fixed to the scalp, segment lengths, a pull back towards the styled shape that weakens towards the tip,
// gravity, damping, the scene's FellowWind3D, the head's sphere and the colliders given), and many more strands drawn,
// each interpolated on the GPU between the three guides nearest its root (Catmull-Rom along them), as ribbons facing
// the camera at least a pixel wide (their coverage in alpha, dithered), lit by a hair model (Kajiya-Kay's diffuse,
// two specular lobes shifted along the strand — Marschner's R and TRT — and light through it from behind).
//
// The scalp: the cap of a sphere (scalp_radius about scalp_center, in the node's space; coverage from its top, the face's
// side left bare by forehead_angle): put the node under a BoneAttachment3D on the head bone. The guides' positions go to
// the GPU in a texture of two halves (this frame's and the last one's, alternating) and each strand's instance data says
// which half is this frame's: Godot's motion vectors read the previous frame's instance data, so TAA and FSR see the
// strands' own motion.
class FellowHair3D : public GeometryInstance3D {
	GDCLASS(FellowHair3D, GeometryInstance3D);

public:
	struct GlobalStats {
		uint64_t frames = 0;
		uint64_t simulate_usec = 0;
		uint64_t upload_usec = 0;
		uint64_t guides = 0;
		uint64_t strands = 0;
	};
	static GlobalStats global_stats;

private:
	// The scalp.
	float scalp_radius = 0.1f;
	Vector3 scalp_center;
	float coverage = 105.0f; // degrees from the top
	float forehead_angle = 55.0f; // degrees about the face's direction (-Z) left bare
	// The strands.
	int guide_count = 160;
	int strand_count = 10000;
	int segments = 12; // a guide's
	float length = 0.32f;
	float length_variation = 0.25f;
	float curl = 0.0f; // the curls' radius (m)
	float curl_frequency = 18.0f; // turns a metre
	float spread = 0.6f; // how far between its guides a strand's root may lie (0: on a guide)
	int seed = 0;
	// The motion.
	bool simulating = true;
	float stiffness = 0.35f; // the pull back to the styled shape at the root (it fades to the tip)
	float damping = 1.5f; // per second
	float gravity_scale = 1.0f;
	float wind_scale = 1.0f;
	float drag = 1.0f;
	float collision_margin = 0.01f; // m beyond the scalp's sphere
	TypedArray<NodePath> colliders;
	int substeps = 4;
	// The look.
	float root_width = 0.4f; // mm (a drawn strand stands for a few real ones)
	float tip_width = 0.12f; // mm
	Color root_color = Color(0.07, 0.045, 0.03);
	Color tip_color = Color(0.22, 0.14, 0.08);
	float color_variation = 0.2f;
	float specular_shift = 0.08f;
	float specular_strength = 0.5f;
	float specular_sharpness = 120.0f;
	float transmission = 0.35f;
	float root_occlusion = 0.6f;

	// The guides: root (node space) and normal, rest points in the node's space, the simulated points (global).
	LocalVector<Vector3> guide_root;
	LocalVector<Vector3> guide_normal;
	LocalVector<Vector3> rest; // guide_count × points, node space
	LocalVector<Vector3> x; // global
	LocalVector<Vector3> prev;
	LocalVector<float> seg_rest; // a guide's segment lengths (the same along it)
	int points = 0;
	bool built = false;
	bool positions_valid = false;
	Transform3D last_xform;
	int parity = 0;

	// The GPU side.
	Ref<MultiMesh> multimesh;
	Ref<ArrayMesh> strip;
	Ref<ShaderMaterial> material;
	Ref<Image> guide_image;
	Ref<ImageTexture> guide_texture;
	Vector<float> instance_data; // the strands' instance data (20 floats each), its parity word updated each frame
	uint64_t last_simulate_usec = 0;
	uint64_t last_upload_usec = 0;

	void _build();
	void _clear();
	void _rebuild();
	void _reset_positions();
	void _simulate(double p_delta);
	void _upload();
	void _update_material();
	void _gather_colliders(LocalVector<Vector4> &r_spheres, LocalVector<Vector3> &r_capsule_a, LocalVector<Vector3> &r_capsule_b, LocalVector<float> &r_capsule_r) const;
	Vector3 _rest_global(int p_guide, int p_point, const Transform3D &p_xform) const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	static String shader_code();
	virtual AABB get_aabb() const override;
	PackedStringArray get_configuration_warnings() const override;

	void reset();
	Dictionary get_stats() const;
	PackedVector3Array get_guide_points(int p_guide) const;
	PackedVector3Array get_guide_rest_points(int p_guide) const;
	int get_point_count() const { return points; }

	void set_scalp_radius(float p_radius);
	float get_scalp_radius() const { return scalp_radius; }
	void set_scalp_center(const Vector3 &p_center);
	Vector3 get_scalp_center() const { return scalp_center; }
	void set_coverage(float p_degrees);
	float get_coverage() const { return coverage; }
	void set_forehead_angle(float p_degrees);
	float get_forehead_angle() const { return forehead_angle; }
	void set_guide_count(int p_count);
	int get_guide_count() const { return guide_count; }
	void set_strand_count(int p_count);
	int get_strand_count() const { return strand_count; }
	void set_segments(int p_segments);
	int get_segments() const { return segments; }
	void set_length(float p_length);
	float get_length() const { return length; }
	void set_length_variation(float p_variation);
	float get_length_variation() const { return length_variation; }
	void set_curl(float p_curl);
	float get_curl() const { return curl; }
	void set_curl_frequency(float p_frequency);
	float get_curl_frequency() const { return curl_frequency; }
	void set_spread(float p_spread);
	float get_spread() const { return spread; }
	void set_seed(int p_seed);
	int get_seed() const { return seed; }
	void set_simulating(bool p_simulating);
	bool is_simulating() const { return simulating; }
	void set_stiffness(float p_stiffness) { stiffness = CLAMP(p_stiffness, 0.0f, 1.0f); }
	float get_stiffness() const { return stiffness; }
	void set_damping(float p_damping) { damping = MAX(0.0f, p_damping); }
	float get_damping() const { return damping; }
	void set_gravity_scale(float p_scale) { gravity_scale = p_scale; }
	float get_gravity_scale() const { return gravity_scale; }
	void set_wind_scale(float p_scale) { wind_scale = p_scale; }
	float get_wind_scale() const { return wind_scale; }
	void set_drag(float p_drag) { drag = MAX(0.0f, p_drag); }
	float get_drag() const { return drag; }
	void set_collision_margin(float p_margin) { collision_margin = MAX(0.0f, p_margin); }
	float get_collision_margin() const { return collision_margin; }
	void set_colliders(const TypedArray<NodePath> &p_colliders) { colliders = p_colliders; }
	TypedArray<NodePath> get_colliders() const { return colliders; }
	void set_substeps(int p_substeps) { substeps = CLAMP(p_substeps, 1, 16); }
	int get_substeps() const { return substeps; }
	void set_root_width(float p_mm);
	float get_root_width() const { return root_width; }
	void set_tip_width(float p_mm);
	float get_tip_width() const { return tip_width; }
	void set_root_color(const Color &p_color);
	Color get_root_color() const { return root_color; }
	void set_tip_color(const Color &p_color);
	Color get_tip_color() const { return tip_color; }
	void set_color_variation(float p_variation);
	float get_color_variation() const { return color_variation; }
	void set_specular_shift(float p_shift);
	float get_specular_shift() const { return specular_shift; }
	void set_specular_strength(float p_strength);
	float get_specular_strength() const { return specular_strength; }
	void set_specular_sharpness(float p_sharpness);
	float get_specular_sharpness() const { return specular_sharpness; }
	void set_transmission(float p_transmission);
	float get_transmission() const { return transmission; }
	void set_root_occlusion(float p_occlusion);
	float get_root_occlusion() const { return root_occlusion; }

	FellowHair3D();
	~FellowHair3D();
};
