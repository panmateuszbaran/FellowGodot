/**************************************************************************/
/*  fellow_gas_volume_3d.h                                                */
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

#include "core/object/ref_counted.h"
#include "core/os/mutex.h"
#include "scene/3d/light_3d.h"
#include "scene/3d/node_3d.h"

class FellowGasEmitter3D;

// The latest data read back from the GPU (written on the render thread, read on the main thread).
class FellowGasReadback : public RefCounted {
	GDCLASS(FellowGasReadback, RefCounted);

public:
	Mutex mutex;
	PackedByteArray data;
	bool fresh = false;
	void store(const PackedByteArray &p_data) {
		MutexLock lock(mutex);
		data = p_data;
		fresh = true;
	}
};

// FellowEngine gas volume (docs/decisions/0117-gas-simulation.md, 0118-gas-rendering.md): smoke and fire simulated on
// the GPU in a box (Eulerian grid, MacCormack advection, buoyancy, vorticity confinement, combustion, a pressure
// projection), emitted by its FellowGasEmitter3D children, flowing around the physics bodies inside it, drawn by the
// Forward+ renderer with self-shadowing, the sun's shadow cast on the scene and the fire's blackbody emission, which also
// lights the scene through an omni light the node places at the fire.
//
// The box spans [-size.x / 2, size.x / 2] x [0, size.y] x [-size.z / 2, size.z / 2] in the node's space (the node at
// its floor's centre); `resolution` cells along its longest side. One simulation step per physics tick (deterministic:
// the same steps for the same ticks), run by the GPU at the start of the next frame.
class FellowGasVolume3D : public Node3D {
	GDCLASS(FellowGasVolume3D, Node3D);

public:
	enum PressureSolver {
		SOLVER_SOR, // red-black SOR, `pressure_iterations` sweeps
		SOLVER_MULTIGRID, // multigrid V-cycles, `multigrid_cycles` of them
	};

	enum Face {
		FACE_NEG_X = 1,
		FACE_POS_X = 2,
		FACE_NEG_Y = 4,
		FACE_POS_Y = 8,
		FACE_NEG_Z = 16,
		FACE_POS_Z = 32,
	};

private:
	// Grid.
	Vector3 size = Vector3(2.0, 4.0, 2.0);
	int resolution = 64;
	bool high_precision = false;
	// Simulation.
	bool simulation_enabled = true;
	int substeps = 1;
	float time_scale = 1.0f;
	int prewarm_steps = 0;
	uint32_t closed_faces = FACE_NEG_Y;
	float buoyancy = 1.0f;
	float smoke_weight = 0.0f;
	float vorticity = 0.3f;
	float turbulence = 0.0f;
	float turbulence_scale = 0.4f;
	Vector3 wind;
	float wind_rate = 0.0f;
	float smoke_dissipation = 0.05f;
	float cooling = 1.2f;
	float ignition_temperature = 250.0f;
	float burn_rate = 6.0f;
	float heat_release = 1400.0f;
	float soot = 0.6f;
	float expansion = 0.4f;
	int pressure_iterations = 20;
	PressureSolver pressure_solver = SOLVER_MULTIGRID;
	int multigrid_cycles = 2;
	bool maccormack = true;
	bool physics_obstacles = true;
	uint32_t collision_mask = 1;
	// Rendering.
	bool render_enabled = true;
	Color smoke_color = Color(0.75, 0.75, 0.75);
	float smoke_density = 4.0f;
	float anisotropy = 0.3f;
	float fire_intensity = 20.0f;
	float fire_temperature_scale = 1.4f;
	float ambient_strength = 1.0f;
	float self_shadow = 1.0f;
	bool cast_shadows = true;
	float shadow_strength = 1.0f;
	float render_step = 1.0f;
	// Fire light.
	bool fire_light = true;
	float fire_light_energy = 1.0f;
	float fire_light_range = 0.0f;
	bool fire_light_shadows = false;

	// State.
	uint32_t volume_id = 0;
	bool configured = false;
	Vector3i grid_res;
	float grid_dx = 0.1f;
	uint64_t ticks = 0;
	double sim_time = 0.0;
	int prewarm_left = 0;
	RID query_shape;
	OmniLight3D *light = nullptr;
	Ref<FellowGasReadback> emission;
	Ref<FellowGasReadback> debug;
	bool debug_pending = false;
	Vector3 fire_light_position;
	Color fire_light_rgb;
	float fire_power = 0.0f;

	void _update_grid();
	Transform3D _grid_to_world() const;
	void _configure();
	void _queue_step(bool p_read_emission);
	void _send_render();
	void _gather_obstacles(void *r_step) const;
	void _gather_emitters(void *r_step) const;
	void _update_fire_light();

	static void _rt_configure(uint32_t p_id, Vector3i p_res, float p_dx, bool p_high_precision, Ref<FellowGasReadback> p_emission);
	static void _rt_free(uint32_t p_id);
	static void _rt_reset(uint32_t p_id);
	static void _rt_step(uint32_t p_id, PackedByteArray p_step);
	static void _rt_render(uint32_t p_id, PackedByteArray p_render);
	static void _rt_debug(uint32_t p_id, int p_what, Ref<FellowGasReadback> p_target);
	static void _rt_store(const PackedByteArray &p_data, Ref<FellowGasReadback> p_target);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_size(const Vector3 &p_size);
	Vector3 get_size() const { return size; }
	void set_resolution(int p_resolution);
	int get_resolution() const { return resolution; }
	void set_high_precision(bool p_enable);
	bool is_high_precision() const { return high_precision; }

	void set_simulation_enabled(bool p_enable) { simulation_enabled = p_enable; }
	bool is_simulation_enabled() const { return simulation_enabled; }
	void set_substeps(int p_substeps) { substeps = CLAMP(p_substeps, 1, 8); }
	int get_substeps() const { return substeps; }
	void set_time_scale(float p_scale) { time_scale = MAX(p_scale, 0.0f); }
	float get_time_scale() const { return time_scale; }
	void set_prewarm_steps(int p_steps) { prewarm_steps = CLAMP(p_steps, 0, 100000); }
	int get_prewarm_steps() const { return prewarm_steps; }
	void set_closed_faces(uint32_t p_faces) { closed_faces = p_faces & 63u; }
	uint32_t get_closed_faces() const { return closed_faces; }
	void set_buoyancy(float p_value) { buoyancy = p_value; }
	float get_buoyancy() const { return buoyancy; }
	void set_smoke_weight(float p_value) { smoke_weight = p_value; }
	float get_smoke_weight() const { return smoke_weight; }
	void set_vorticity(float p_value) { vorticity = MAX(p_value, 0.0f); }
	float get_vorticity() const { return vorticity; }
	void set_turbulence(float p_value) { turbulence = MAX(p_value, 0.0f); }
	float get_turbulence() const { return turbulence; }
	void set_turbulence_scale(float p_value) { turbulence_scale = MAX(p_value, 0.001f); }
	float get_turbulence_scale() const { return turbulence_scale; }
	void set_wind(const Vector3 &p_wind) { wind = p_wind; }
	Vector3 get_wind() const { return wind; }
	void set_wind_rate(float p_value) { wind_rate = MAX(p_value, 0.0f); }
	float get_wind_rate() const { return wind_rate; }
	void set_smoke_dissipation(float p_value) { smoke_dissipation = MAX(p_value, 0.0f); }
	float get_smoke_dissipation() const { return smoke_dissipation; }
	void set_cooling(float p_value) { cooling = MAX(p_value, 0.0f); }
	float get_cooling() const { return cooling; }
	void set_ignition_temperature(float p_value) { ignition_temperature = MAX(p_value, 0.0f); }
	float get_ignition_temperature() const { return ignition_temperature; }
	void set_burn_rate(float p_value) { burn_rate = MAX(p_value, 0.0f); }
	float get_burn_rate() const { return burn_rate; }
	void set_heat_release(float p_value) { heat_release = MAX(p_value, 0.0f); }
	float get_heat_release() const { return heat_release; }
	void set_soot(float p_value) { soot = MAX(p_value, 0.0f); }
	float get_soot() const { return soot; }
	void set_expansion(float p_value) { expansion = MAX(p_value, 0.0f); }
	float get_expansion() const { return expansion; }
	void set_pressure_iterations(int p_value) { pressure_iterations = CLAMP(p_value, 0, 200); }
	int get_pressure_iterations() const { return pressure_iterations; }
	void set_pressure_solver(PressureSolver p_solver) { pressure_solver = p_solver; }
	PressureSolver get_pressure_solver() const { return pressure_solver; }
	void set_multigrid_cycles(int p_cycles) { multigrid_cycles = CLAMP(p_cycles, 1, 8); }
	int get_multigrid_cycles() const { return multigrid_cycles; }
	void set_maccormack(bool p_enable) { maccormack = p_enable; }
	bool is_maccormack() const { return maccormack; }
	void set_physics_obstacles(bool p_enable) { physics_obstacles = p_enable; }
	bool is_physics_obstacles() const { return physics_obstacles; }
	void set_collision_mask(uint32_t p_mask) { collision_mask = p_mask; }
	uint32_t get_collision_mask() const { return collision_mask; }

	void set_render_enabled(bool p_enable) { render_enabled = p_enable; }
	bool is_render_enabled() const { return render_enabled; }
	void set_smoke_color(const Color &p_color) { smoke_color = p_color; }
	Color get_smoke_color() const { return smoke_color; }
	void set_smoke_density(float p_value) { smoke_density = MAX(p_value, 0.0f); }
	float get_smoke_density() const { return smoke_density; }
	void set_anisotropy(float p_value) { anisotropy = CLAMP(p_value, -0.95f, 0.95f); }
	float get_anisotropy() const { return anisotropy; }
	void set_fire_intensity(float p_value) { fire_intensity = MAX(p_value, 0.0f); }
	float get_fire_intensity() const { return fire_intensity; }
	void set_fire_temperature_scale(float p_value) { fire_temperature_scale = CLAMP(p_value, 0.25f, 4.0f); }
	float get_fire_temperature_scale() const { return fire_temperature_scale; }
	void set_ambient_strength(float p_value) { ambient_strength = MAX(p_value, 0.0f); }
	float get_ambient_strength() const { return ambient_strength; }
	void set_self_shadow(float p_value) { self_shadow = MAX(p_value, 0.0f); }
	float get_self_shadow() const { return self_shadow; }
	void set_cast_shadows(bool p_enable) { cast_shadows = p_enable; }
	bool is_casting_shadows() const { return cast_shadows; }
	void set_shadow_strength(float p_value) { shadow_strength = MAX(p_value, 0.0f); }
	float get_shadow_strength() const { return shadow_strength; }
	void set_render_step(float p_value) { render_step = CLAMP(p_value, 0.25f, 4.0f); }
	float get_render_step() const { return render_step; }

	void set_fire_light(bool p_enable);
	bool is_fire_light() const { return fire_light; }
	void set_fire_light_energy(float p_value) { fire_light_energy = MAX(p_value, 0.0f); }
	float get_fire_light_energy() const { return fire_light_energy; }
	void set_fire_light_range(float p_value) { fire_light_range = MAX(p_value, 0.0f); }
	float get_fire_light_range() const { return fire_light_range; }
	void set_fire_light_shadows(bool p_enable) { fire_light_shadows = p_enable; }
	bool is_fire_light_shadows() const { return fire_light_shadows; }

	void reset_simulation();
	Vector3i get_grid_size() const { return grid_res; }
	float get_cell_size() const { return grid_dx; }
	int64_t get_vram_bytes() const;
	int64_t get_steps_simulated() const { return int64_t(ticks); }
	float get_fire_power() const { return fire_power; }
	OmniLight3D *get_fire_light_node() const { return light; }

	// Tests: the GPU state (0 scalars, 1 velocity, 2 pressure, 3 the divergence's right-hand side now, 4 the solid
	// cells) read back asynchronously; poll is_debug_data_ready().
	void request_debug_data(int p_what);
	bool is_debug_data_ready() const;
	PackedByteArray get_debug_data() const;

	FellowGasVolume3D();
	~FellowGasVolume3D();
};

VARIANT_ENUM_CAST(FellowGasVolume3D::Face);
VARIANT_ENUM_CAST(FellowGasVolume3D::PressureSolver);
