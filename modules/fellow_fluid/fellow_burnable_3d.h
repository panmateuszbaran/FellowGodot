/**************************************************************************/
/*  fellow_burnable_3d.h                                                  */
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

#include "fellow_gas_volume_3d.h"

#include "core/templates/hash_map.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/material.h"
#include "scene/resources/mesh.h"
#include "scene/resources/texture_rd.h"

class MeshInstance3D;

// FellowEngine burning object (docs/decisions/0120-burning.md): makes a MeshInstance3D (its `mesh` path; empty: the
// parent) burn like wood. The mesh is voxelized into a box of cubic voxels (`resolution` along its longest side), each
// with a temperature and how much of it is wood, char and ash; the GPU heats it from the gas volume it is in (the
// flames' temperature), conducts the heat inwards, pyrolyses the wood to char (the volatiles feed the gas's fire: the
// flames come from the object), oxidises the exposed char to ash (glowing embers) and lets the ash settle. The mesh
// recedes with the material (its vertices march into the voxels), chars, glows in its cracks and ends as a heap of ash.
// The gas flows around the object's voxels (its physics body is not the gas's obstacle).
//
// Runs only in a running game (not in the editor); it replaces the mesh instance's mesh and materials while in the
// tree (restored when it leaves).
class FellowBurnable3D : public Node3D {
	GDCLASS(FellowBurnable3D, Node3D);

private:
	// Target and grid.
	NodePath mesh_path;
	NodePath gas_volume_path; // empty: the first FellowGasVolume3D whose box holds the object's centre
	int resolution = 32;
	bool subdivide = true;
	bool draw_burning = true;
	bool simulation_enabled = true;
	float burn_speed = 10.0f;
	// Material (softwood; temperatures in kelvin).
	float density = 500.0f;
	float specific_heat = 1700.0f;
	float conductivity = 0.15f;
	float pyrolysis_temperature = 573.0f;
	float pyrolysis_rate = 0.05f;
	float char_yield = 0.25f;
	float glow_temperature = 673.0f;
	float glow_rate = 0.02f;
	float ash_volume = 0.15f;
	// Coupling with the gas.
	float flame_strength = 1.0f;
	float smoke = 0.05f;
	float surface_heating = 200.0f;
	float flame_attachment = 0.5f;
	float flame_flicker = 0.7f;
	float flame_temperature = 1100.0f; // K: the surface's own flame
	float flame_reach = 0.1f; // m: how far above the burning surface its flame heats
	float collapse_fraction = 0.2f; // the physics body stops colliding when less wood and char is left (0: never)
	// Look.
	Color char_color = Color(0.045, 0.038, 0.034);
	Color ash_color = Color(0.56, 0.54, 0.51);
	float glow_intensity = 4.0f;
	float crack_scale = 0.012f; // m

	// State.
	uint32_t burn_id = 0;
	bool configured = false;
	bool failed = false;
	ObjectID target_id;
	Ref<Mesh> original_mesh;
	Vector<Ref<Material>> original_overrides;
	Ref<Material> original_material_override;
	Ref<ArrayMesh> display_mesh;
	Vector<Ref<ShaderMaterial>> burn_materials;
	Ref<Texture3DRD> display_texture;
	Ref<FellowGasReadback> totals;
	Ref<FellowGasReadback> debug;
	ObjectID body_id;
	// The voxels (the mesh's local space).
	Vector3i res;
	float voxel = 0.05f; // local units
	Vector3 origin; // the box's corner
	Vector3 grain = Vector3(0.0f, 1.0f, 0.0f); // the wood's grain (the mesh's longest axis, local)
	Vector<float> initial_fill;
	int occupied_min[3] = { 0, 0, 0 };
	int occupied_max[3] = { 0, 0, 0 };
	int ground_axis = -1; // the box reaches down to the ground along this voxel axis (the ash falls to it)
	int ground_sign = -1;
	int ground_floor = 0; // the layer on the ground
	int heap_axis = 1; // the voxel axis nearest to the world's down at configuration, its sign, the ash's floor layer
	int heap_sign = -1;
	int heap_floor = 0;
	ObjectID heap_id; // the ash heap's MeshInstance3D (an internal child of the target)
	Ref<ShaderMaterial> heap_material;
	double initial_volume = 0.0; // voxels
	// Totals (read back every few ticks).
	float wood_fraction = 1.0f;
	float char_fraction = 0.0f;
	float ash_fraction = 0.0f;
	float max_temperature = 293.0f;
	int burning_voxels = 0;
	float fuel_release = 0.0f;
	bool has_ignited = false;
	bool has_burned_out = false;
	bool has_collapsed = false;
	LocalVector<ObjectID> disabled_shapes; // the body's shapes this node disabled (collapsed)
	bool froze_body = false;
	uint64_t ticks = 0;
	LocalVector<Vector4> pending_ignitions; // centre (voxels), radius (voxels)
	LocalVector<float> pending_ignition_temperatures; // K above ambient

	static Mutex body_mutex;
	static HashMap<uint64_t, int> burnable_bodies;

	MeshInstance3D *_get_target() const;
	FellowGasVolume3D *_find_gas_volume() const;
	bool _voxelize(const Ref<Mesh> &p_mesh);
	bool _ground_distance(MeshInstance3D *p_target, const AABB &p_box, int &r_axis, int &r_sign, float &r_below) const; // the ground below the box
	void _build_display(MeshInstance3D *p_target);
	Ref<ShaderMaterial> _make_burn_material(const Ref<Material> &p_source, int p_custom) const;
	void _update_materials();
	void _swap_in(MeshInstance3D *p_target);
	void _build_heap(MeshInstance3D *p_target);
	void _configure();
	void _unconfigure();
	void _queue_step();
	void _read_totals();
	void _collapse();
	void _restore_collision();
	float _fill_at(const Vector3 &p_local) const; // the initial fill, trilinear (as the GPU's display texture)

	static void _rt_configure(uint32_t p_id, Vector3i p_res, PackedByteArray p_initial, Ref<FellowGasReadback> p_totals, Ref<Texture3DRD> p_texture);
	static void _rt_free(uint32_t p_id);
	static void _rt_step(uint32_t p_id, PackedByteArray p_step);
	static void _rt_debug(uint32_t p_id, int p_what, Ref<FellowGasReadback> p_target);
	static void _rt_store(const PackedByteArray &p_data, Ref<FellowGasReadback> p_target);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	// Whether a physics body belongs to a burning object (the gas volumes skip it as an obstacle).
	static bool is_burnable_body(uint64_t p_object_id);

	void set_mesh_path(const NodePath &p_path);
	NodePath get_mesh_path() const { return mesh_path; }
	void set_gas_volume_path(const NodePath &p_path) { gas_volume_path = p_path; }
	NodePath get_gas_volume_path() const { return gas_volume_path; }
	void set_resolution(int p_resolution);
	int get_resolution() const { return resolution; }
	void set_subdivide(bool p_enable);
	bool is_subdivide() const { return subdivide; }
	void set_draw_burning(bool p_enable);
	bool is_draw_burning() const { return draw_burning; }
	void set_simulation_enabled(bool p_enable) { simulation_enabled = p_enable; }
	bool is_simulation_enabled() const { return simulation_enabled; }
	void set_burn_speed(float p_speed) { burn_speed = CLAMP(p_speed, 0.0f, 1000.0f); }
	float get_burn_speed() const { return burn_speed; }

	void set_density(float p_value) { density = MAX(p_value, 1.0f); }
	float get_density() const { return density; }
	void set_specific_heat(float p_value) { specific_heat = MAX(p_value, 1.0f); }
	float get_specific_heat() const { return specific_heat; }
	void set_conductivity(float p_value) { conductivity = MAX(p_value, 0.0f); }
	float get_conductivity() const { return conductivity; }
	void set_pyrolysis_temperature(float p_value) { pyrolysis_temperature = MAX(p_value, 294.0f); }
	float get_pyrolysis_temperature() const { return pyrolysis_temperature; }
	void set_pyrolysis_rate(float p_value) { pyrolysis_rate = MAX(p_value, 0.0f); }
	float get_pyrolysis_rate() const { return pyrolysis_rate; }
	void set_char_yield(float p_value) { char_yield = CLAMP(p_value, 0.0f, 1.0f); }
	float get_char_yield() const { return char_yield; }
	void set_glow_temperature(float p_value) { glow_temperature = MAX(p_value, 294.0f); }
	float get_glow_temperature() const { return glow_temperature; }
	void set_glow_rate(float p_value) { glow_rate = MAX(p_value, 0.0f); }
	float get_glow_rate() const { return glow_rate; }
	void set_ash_volume(float p_value) {
		ash_volume = CLAMP(p_value, 0.0f, 1.0f);
		_update_materials();
	}
	float get_ash_volume() const { return ash_volume; }
	void set_flame_strength(float p_value) { flame_strength = MAX(p_value, 0.0f); }
	float get_flame_strength() const { return flame_strength; }
	void set_smoke(float p_value) { smoke = MAX(p_value, 0.0f); }
	float get_smoke() const { return smoke; }
	void set_surface_heating(float p_value) { surface_heating = MAX(p_value, 0.0f); }
	float get_surface_heating() const { return surface_heating; }
	void set_flame_attachment(float p_value) { flame_attachment = CLAMP(p_value, 0.0f, 1.0f); }
	float get_flame_attachment() const { return flame_attachment; }
	void set_flame_flicker(float p_value) { flame_flicker = CLAMP(p_value, 0.0f, 1.0f); }
	float get_flame_flicker() const { return flame_flicker; }
	void set_flame_temperature(float p_value) { flame_temperature = CLAMP(p_value, 0.0f, 3000.0f); }
	float get_flame_temperature() const { return flame_temperature; }
	void set_flame_reach(float p_value) { flame_reach = CLAMP(p_value, 0.0f, 10.0f); }
	float get_flame_reach() const { return flame_reach; }
	void set_collapse_fraction(float p_value) { collapse_fraction = CLAMP(p_value, 0.0f, 1.0f); }
	float get_collapse_fraction() const { return collapse_fraction; }
	bool is_collapsed() const { return has_collapsed; }

	void set_char_color(const Color &p_color);
	Color get_char_color() const { return char_color; }
	void set_ash_color(const Color &p_color);
	Color get_ash_color() const { return ash_color; }
	void set_glow_intensity(float p_value);
	float get_glow_intensity() const { return glow_intensity; }
	void set_crack_scale(float p_value);
	float get_crack_scale() const { return crack_scale; }

	// Heats the material within `radius` metres of a point (global) to at least `temperature` kelvin.
	void ignite(const Vector3 &p_position, float p_radius, float p_temperature = 900.0f);
	void reset_burning();

	float get_wood_fraction() const { return wood_fraction; } // of the initial volume
	float get_char_fraction() const { return char_fraction; }
	float get_ash_fraction() const { return ash_fraction; }
	float get_max_temperature() const { return max_temperature; } // kelvin
	int get_burning_voxels() const { return burning_voxels; }
	bool is_burning() const { return burning_voxels > 0; }
	bool is_burned_out() const { return has_burned_out; }
	Vector3i get_voxel_grid_size() const { return res; }
	float get_voxel_size() const { return voxel; } // the mesh's local units
	Vector3 get_voxel_origin() const { return origin; }
	PackedFloat32Array get_initial_fill() const;
	int get_display_triangle_count() const;
	int64_t get_steps_simulated() const { return int64_t(ticks); }

	// Tests: the GPU state (0 the voxels RGBA32F: temperature above ambient, wood, char, ash; 1 the emission RGBA16F;
	// 2 the gas region's injection RGBA16F) read back asynchronously; poll is_debug_data_ready().
	void request_debug_data(int p_what);
	bool is_debug_data_ready() const;
	PackedByteArray get_debug_data() const;
	Dictionary get_last_step_info() const; // the transforms and parameters of the last step queued

	FellowBurnable3D();
	~FellowBurnable3D();

private:
	Dictionary last_step;
};
