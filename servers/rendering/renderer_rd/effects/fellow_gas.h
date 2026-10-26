/**************************************************************************/
/*  fellow_gas.h                                                          */
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

#include "core/math/projection.h"
#include "core/math/transform_3d.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/variant/callable.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_burn_sim.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_gas_render.glsl.gen.h"
#include "servers/rendering/renderer_rd/shaders/effects/fellow_gas_sim.glsl.gen.h"
#include "servers/rendering/rendering_device.h"

namespace RendererRD {

// FellowEngine gas volumes: smoke and fire (docs/decisions/0117-gas-simulation.md, 0118-gas-rendering.md). The scene
// side is modules/fellow_fluid's FellowGasVolume3D, which calls this on the render thread: one volume per node, its
// simulation state on the GPU (fellow_gas_sim.glsl), its steps queued and run at the start of the next frame (inside
// the frame's timestamps); the Forward+ renderer draws the volumes (fellow_gas_render.glsl): their self-shadow and the
// shadow they cast before the opaque passes, a half-resolution ray march composited before the transparent pass.
class FellowGas {
public:
	static constexpr int MAX_EMITTERS = 16;
	static constexpr int MAX_OBSTACLES = 16;
	static constexpr int MAX_RENDERED = 8;
	static constexpr int MAX_MG_LEVELS = 12; // the grid and its coarser levels (fellow_gas_sim.glsl's MAX_MG_LEVELS)
	static constexpr int MAX_BURNABLES = 8; // burning objects coupled with one gas volume
	static constexpr float BURN_FLAME_LIMIT = 2200.0f; // K above ambient: the gas's flame limit where burning objects feed it and none is set

	// fellow_gas_sim.glsl's Emitter and Obstacle (std140), in the volume's grid (cells).
	struct GPUEmitter {
		float centre_radius[4];
		float rot0[4];
		float rot1[4];
		float rot2[4];
		float rates[4];
		float velocity[4];
		float shape[4];
	};
	struct GPUObstacle {
		float centre_type[4];
		float rot0[4];
		float rot1[4];
		float rot2[4];
		float linear_velocity[4];
		float angular_velocity[4];
	};

	// One step of a volume (plain data: the node passes it through a PackedByteArray).
	struct StepParams {
		float dt = 1.0f / 60.0f;
		float up[3] = { 0.0f, 1.0f, 0.0f }; // world up in the grid's axes
		float buoyancy = 1.0f;
		float wind[3] = { 0.0f, 0.0f, 0.0f }; // m/s, the grid's axes
		float wind_rate = 0.0f; // 1/s
		float smoke_weight = 0.0f;
		float vorticity = 0.3f;
		float turbulence = 0.0f; // m/s^2
		float turbulence_frequency = 0.15f; // 1/cell
		float smoke_dissipation = 0.05f; // 1/s
		float cooling = 1.2f; // 1/s
		float ignition = 250.0f; // K above ambient
		float flame_limit = 0.0f; // K above ambient: the hottest the combustion makes the gas (0: none, BURN_FLAME_LIMIT with burning objects)
		float burn_rate = 6.0f; // 1/s
		float heat = 1400.0f; // K per unit fuel
		float soot = 0.6f; // smoke per unit fuel
		float expansion = 0.4f;
		float omega = 1.6f;
		float time = 0.0f;
		float emission_intensity = 20.0f;
		float emission_temperature_scale = 1.4f;
		float max_speed_cells = 6.0f;
		float ambient_temperature = 293.0f;
		int32_t iterations = 20; // SOR's iterations
		int32_t multigrid = 1; // 1: the pressure by multigrid V-cycles (red-black Gauss-Seidel smoothing), 0: SOR
		int32_t multigrid_cycles = 2;
		int32_t sparse = 0; // 1: only the bricks near smoke, heat, fuel or an emitter are simulated
		int32_t sparse_margin = 2; // bricks around the sources
		uint32_t closed_faces = 1u << 2; // bit per face: -x +x -y +y -z +z
		int32_t maccormack = 1;
		int32_t emitter_count = 0;
		int32_t obstacle_count = 0;
		int32_t read_emission = 0;
		int32_t shift[3] = { 0, 0, 0 }; // the box moved by whole cells before this step (new cell c = old cell c + shift)
		GPUEmitter emitters[MAX_EMITTERS];
		GPUObstacle obstacles[MAX_OBSTACLES];
		uint64_t tick = 0; // the physics frame (the volumes' and the burnables' steps run in its order)
	};

	// One step of a burning object (FellowBurnable3D, decision 0120; plain data). Temperatures in K above ambient.
	struct BurnStepParams {
		float dt = 0.0f; // burning time (the physics tick times the node's burn speed)
		float voxel_size = 0.05f; // m
		float settle = 0.0f; // the ash settled this step (1 - exp(-rate dt))
		float densities[4] = {}; // wood, char, ash (kg/m^3), the smallest heat capacity
		float heats[4] = {}; // wood, char, ash specific heat, pyrolysis heat
		float conduct[4] = {}; // wood, char, ash conductivity, convection
		float pyro[4] = {}; // onset, rate, char yield, char volume
		float glow[4] = {}; // onset, rate, heat kept, ash volume
		float misc[4] = {}; // emissivity, fuel per kg, gas sample distance (voxels), ambient (K)
		uint32_t gas_id = 0; // the gas volume it burns in (0: none)
		float voxel_to_gas[12] = {}; // rows: voxel coordinates -> the gas grid's
		float gas_to_voxel[12] = {};
		int32_t region_origin[3] = {}; // the gas cells it injects into
		int32_t region_size[3] = {};
		float footprint = 2.0f; // gas cells
		float smoke_per_fuel = 0.3f;
		float heat_rate = 20.0f; // the air's heating at full exposure (1/s)
		float attached = 0.5f; // the fraction of the fuel burning where it is released (the flame on the surface)
		float ignite[4] = {}; // centre (voxels), radius (voxels; 0 none)
		float ignite_temperature = 0.0f;
		int32_t settle_axis = 1; // the ash falls along this voxel axis (the one nearest to the world's down),
		int32_t settle_sign = -1; // in this direction,
		int32_t settle_floor = 0; // down to this layer (the object's lowest)
		float flame_heat = 0.0f; // the volatiles' flame's heat returned to the surface (J per kg of volatiles)
		int32_t read_totals = 0;
		uint64_t tick = 0; // the physics frame (the gas's and the burnables' steps run in its order)
	};

	// How a volume is drawn (plain data).
	struct RenderParams {
		Transform3D grid_to_world; // grid (cells) to world
		float albedo[3] = { 0.8f, 0.8f, 0.8f };
		float density = 4.0f; // extinction per unit smoke per metre
		float emission_intensity = 20.0f;
		float emission_temperature_scale = 1.4f;
		float ambient_temperature = 293.0f;
		float anisotropy = 0.3f;
		float ambient = 1.0f;
		float self_shadow = 1.0f;
		float scene_shadow = 1.0f;
		float step_cells = 1.0f;
		float fire_light_position[3] = { 0.0f, 0.0f, 0.0f };
		float fire_light_intensity[3] = { 0.0f, 0.0f, 0.0f };
		int32_t fire_light = 0;
		int32_t casts_shadows = 1;
		int32_t visible = 1;
	};

	// What the renderer gives a frame.
	struct FrameParams {
		Projection view_projection; // corrected (jittered) projection * view
		Vector3 camera_position;
		Vector3 sun_direction; // towards the sun
		Color sun_illuminance;
		bool has_sun = false;
		// The environment's ambient light (RenderForwardClustered::FellowRTLighting's).
		Color ambient_color; // a: the sky's contribution
		bool use_ambient = false;
		bool use_sky = false;
		float sky_factor = 1.0f;
		Basis sky_xform;
		RID radiance;
		float radiance_border[2] = { 0.0f, 1.0f };
		float radiance_level = 0.0f;
		float exposure = 1.0f; // the emission's exposure normalization
		Transform3D view; // world -> view
		bool sun_shadow = false; // the sun's shadow map darkens the smoke (directional light 0 casts shadows)
		RID directional_lights; // LightStorage's directional light buffer
		RID shadow_atlas; // the directional shadow atlas
		Size2i size;
		uint32_t frame = 0;
	};

private:
	struct SimUBO { // std140, fellow_gas_sim.glsl's Params
		int32_t size[4];
		float grid[4];
		float up[4];
		float wind[4];
		float forces[4];
		float decay[4];
		float combustion[4];
		int32_t flags[4];
		float misc[4];
		float emission[4];
		int32_t sparse[4]; // on, F's start in the brick list, margin, every brick counts as simulated before
		GPUEmitter emitters[MAX_EMITTERS];
		GPUObstacle obstacles[MAX_OBSTACLES];
		int32_t mg_levels[MAX_MG_LEVELS][4]; // xyz size, w x offset in the atlas
		float mg_open_lo[MAX_MG_LEVELS][4]; // the open faces' weights, w dx^2
		float mg_open_hi[MAX_MG_LEVELS][4];
		int32_t burn_count[4];
		int32_t burn_origin[MAX_BURNABLES][4];
		int32_t burn_size[MAX_BURNABLES][4];
		float burn_params[MAX_BURNABLES][4];
	};

	struct BurnUBO { // std140, fellow_burn_sim.glsl's Params
		int32_t size[4];
		float grid[4];
		float densities[4];
		float heats[4];
		float conduct[4];
		float pyro[4];
		float glow[4];
		float misc[4];
		float voxel_to_gas[3][4];
		int32_t gas_size[4];
		float ignite[4];
		float ignite_temperature[4];
		int32_t region_origin[4];
		int32_t region_size[4];
		float gas_to_voxel[3][4];
		float inject[4];
		int32_t settle[4];
		float flame[4];
	};
	enum BurnMode {
		BURN_IGNITE,
		BURN_HEAT,
		BURN_SETTLE,
		BURN_INJECT,
		BURN_REDUCE,
		BURN_MAX,
	};
	struct Burnable {
		Vector3i res;
		RID state[2]; // RGBA32F: temperature, wood, char, ash
		RID display; // RGBA16F copy (the mesh's material samples it)
		RID emission; // RGBA16F: fuel released, surface temperature, exposure, solid
		RID inject; // RGBA16F, the gas cells' region
		Vector3i inject_size;
		RID ubo;
		RID totals;
		uint32_t current = 0;
		LocalVector<BurnStepParams> pending;
		Callable totals_callback;
		bool totals_in_flight = false;
		// What the gas reads (the last step's).
		uint32_t gas_id = 0;
		int32_t region_origin[3] = {};
		int32_t region_size[3] = {};
		float smoke_per_fuel = 0.0f;
		float heat_rate = 0.0f;
		float attached = 0.0f;
		bool injected = false;
	};

	struct RenderVolumeUBO { // std140, fellow_gas_render.glsl's Volume
		float w2g0[4];
		float w2g1[4];
		float w2g2[4];
		int32_t size[4];
		int32_t light_size[4];
		float albedo_density[4];
		float emission[4];
		float fire_light_position[4];
		float fire_light_intensity[4];
		float misc[4];
		float fire[4];
	};
	struct RenderUBO {
		float inv_view_projection[16];
		float camera_position[4];
		float sun_direction[4];
		float sun_illuminance[4];
		float ambient_color[4];
		float sky_params[4];
		float radiance_border[4];
		float sky_xform[16];
		float view[16];
		int32_t sizes[4];
		int32_t rect[4];
		int32_t counts[4];
		RenderVolumeUBO volumes[MAX_RENDERED];
	};

	enum SimMode {
		SIM_OBSTACLES,
		SIM_EMIT,
		SIM_ADVECT_S1,
		SIM_ADVECT_S2,
		SIM_ADVECT_V1,
		SIM_ADVECT_V2,
		SIM_CURL,
		SIM_CELL_FORCE,
		SIM_FACE_FORCE,
		SIM_DIVERGENCE,
		SIM_PRESSURE,
		SIM_PROJECT,
		SIM_COARSE,
		SIM_MG_RESTRICT,
		SIM_MG_PROLONG,
		SIM_MG_COARSE,
		SIM_EXPORT,
		SIM_SHIFT,
		SIM_MG_TAIL,
		SIM_SPARSE_SOURCE,
		SIM_SPARSE_ACTIVATE,
		SIM_SPARSE_LIST1,
		SIM_SPARSE_CLEAR,
		SIM_MAX,
	};
	enum RenderMode {
		RENDER_LIGHT,
		RENDER_SHADOW,
		RENDER_TRACE,
		RENDER_TRACE_ARRAY, // the sky's radiance in an octahedral map array
		RENDER_COMPOSITE,
		RENDER_MAX,
	};

	struct Level { // a coarser level of the multigrid pressure solve (its textures: the volume's atlases)
		Vector3i size;
		int offset_x = 0; // where it starts in the atlases
		RID pressure;
		RID rhs;
		RID solid;
	};

	struct Volume {
		LocalVector<Level> levels; // the multigrid's coarser levels, the coarsest last
		RID mg_pressure; // the coarser levels side by side along x (one texture each: the small levels in one pass)
		RID mg_rhs;
		RID mg_solid;
		// Sparse bricks (4^3 cells; the velocity's texels t in brick t / 4).
		RID brick_source; // R8UI, per brick of the cells: a source of smoke, heat, fuel or an emitter
		RID brick_mask; // R8UI, per brick of the velocity: bit 0 A (cells simulated), bit 1 F (velocity simulated)
		RID brick_list; // A's bricks, then F's (from bricks_v), the first coarser level's (from 2 bricks_v): x | y << 10 | z << 20
		RID brick_args; // the indirect dispatches' workgroups: A, 1, 1, F, 1, 1, level 1, 1, 1
		int bricks_v = 0;
		bool sparse_now = false; // this step runs sparse
		bool sparse_last = false; // the last step ran sparse (its masks are valid)
		Vector3i res;
		float dx = 0.1f;
		bool high_precision = false;
		RID scalars[2];
		RID scalars_hat; // also the vorticity
		RID velocity[2];
		RID velocity_hat; // also the cells' forces
		RID pressure;
		RID rhs;
		RID solid;
		RID coarse;
		RID light;
		RID emission_buffer;
		RID velocity_export; // RGBA16F, the cell-centred velocity for GPU particles (when asked for)
		float velocity_export_scale = 5.0f;
		RID ubo;
		uint32_t current = 0;
		uint64_t steps = 0;
		uint64_t light_step = UINT64_MAX; // the step the light volume was computed for
		Vector3 light_sun;
		RenderParams render;
		bool render_set = false;
		LocalVector<StepParams> pending;
		Callable emission_callback;
		bool emission_in_flight = false;
		bool needs_clear = true;
	};

	static FellowGas *singleton;
	static SafeNumeric<uint32_t> next_id;

	FellowGasSimShaderRD sim_shader[2]; // half and full precision
	RID sim_version[2];
	RID sim_pipelines[2][SIM_MAX];
	bool sim_ready[2] = { false, false };
	FellowGasRenderShaderRD render_shader;
	RID render_version;
	RID render_pipelines[RENDER_MAX];
	RID render_ubo;
	RID dummy_3d;
	RID linear_sampler; // clamped in u, v and w
	RID linear_mip_sampler;
	RID nearest_sampler;
	Size2i trace_size;

	HashMap<uint32_t, Volume> volumes;
	HashMap<uint32_t, Burnable> burnables;
	FellowBurnSimShaderRD burn_shader;
	RID burn_version;
	RID burn_pipelines[BURN_MAX];
	bool burn_ready = false;
	LocalVector<RD::Uniform> step_burn_uniforms; // the gas step's burnables (binding 16)
	// The frame's volumes in the render UBO's order (near to far), from begin_frame().
	LocalVector<uint32_t> frame_volumes;
	FrameParams frame;
	Rect2i frame_rect; // the screen rectangle the frame's volumes cover (full resolution)
	bool frame_active = false;

	void _ensure_sim(int p_precision);
	void _free_volume_textures(Volume &r_v);
	void _allocate(Volume &r_v);
	void _clear(Volume &r_v);
	void _run_step(uint32_t p_id, Volume &r_v, const StepParams &p_step);
	struct MGPush { // fellow_gas_sim.glsl's Push (the pressure's levels)
		int32_t size[4];
		float level[4];
		int32_t coarse_size[4];
		float open_lo[4]; // the open faces' weights in the level's stencil
		float open_hi[4];
		int32_t offsets[4]; // x the level's offset in the atlas (RESTRICT, PROLONG: the finer's), y the coarser's
	};
	static void _open_weights(const Vector3i &p_res, int p_level, float *r_lo, float *r_hi);
	void _open_weights(const Vector3i &p_res, int p_level, MGPush &r_push) const;
	enum BrickList {
		LIST_NONE, // dense
		LIST_A, // sparse: one workgroup per brick of A
		LIST_F,
		LIST_L1, // the first coarser level's bricks with a cell of A's below
	};
	void _dispatch_sim(Volume &r_v, SimMode p_mode, const Vector<RD::Uniform> &p_uniforms, const Vector3i &p_threads, const MGPush *p_push = nullptr, bool p_groups = false, BrickList p_list = LIST_NONE);
	void _smooth(Volume &r_v, const Vector3i &p_size, int p_level, RID p_pressure, RID p_rhs, RID p_solid, float p_dx2, float p_omega, int p_iterations);
	static void _emission_read(const Vector<uint8_t> &p_data, Callable p_callback, uint32_t p_id);
	void _ensure_burn();
	void _free_burnable(Burnable &r_b);
	void _run_burn_step(uint32_t p_id, Burnable &r_b, const BurnStepParams &p_step);
	void _dispatch_burn(Burnable &r_b, BurnMode p_mode, const Vector<RD::Uniform> &p_uniforms, const Vector3i &p_threads);
	int _gather_burnables(uint32_t p_volume, SimUBO &r_ubo); // the burnables injecting into a volume (fills step_burn_uniforms)
	static void _burn_totals_read(const Vector<uint8_t> &p_data, Callable p_callback, uint32_t p_id);

public:
	static FellowGas *get_singleton(); // created on first use (render thread); nullptr without a RenderingDevice
	static FellowGas *get_singleton_if_exists() { return singleton; }
	static bool rendering_enabled(); // rendering/fellow/fluid/gas_rendering (and FellowEngine on)
	static void finalize();
	static uint32_t allocate_id(); // any thread
	static int64_t estimate_vram_bytes(const Vector3i &p_res, bool p_high_precision);

	// Render thread.
	void volume_configure(uint32_t p_id, const Vector3i &p_res, float p_dx, bool p_high_precision);
	void volume_free(uint32_t p_id);
	void volume_reset(uint32_t p_id);
	void volume_queue_step(uint32_t p_id, const StepParams &p_step);
	void volume_set_render(uint32_t p_id, const RenderParams &p_render);
	void volume_set_emission_callback(uint32_t p_id, const Callable &p_callback); // called with (PackedByteArray)
	// Debug and tests: the textures' contents (0 scalars, 1 velocity, 2 pressure, 3 the divergence's right-hand side
	// recomputed from the current velocity, 4 the solid cells), asynchronously; the callback gets (PackedByteArray).
	void volume_debug_read(uint32_t p_id, int p_what, const Callable &p_callback);
	uint64_t volume_get_steps(uint32_t p_id) const;
	// The velocity at the cell centres as a texture for GPU particles' vector field attractors (2 * texel - 1 =
	// v / p_scale), updated after every step; RID() turns it off.
	RID volume_enable_velocity_export(uint32_t p_id, bool p_enable, float p_scale);

	// Burning objects (FellowBurnable3D; render thread).
	void burnable_configure(uint32_t p_id, const Vector3i &p_res, const Vector<uint8_t> &p_initial); // RGBA32F texels
	void burnable_free(uint32_t p_id);
	void burnable_queue_step(uint32_t p_id, const BurnStepParams &p_step);
	RID burnable_get_display(uint32_t p_id) const; // RGBA16F state for the mesh's material
	void burnable_set_totals_callback(uint32_t p_id, const Callable &p_callback); // (PackedByteArray) per workgroup
	void burnable_debug_read(uint32_t p_id, int p_what, const Callable &p_callback); // 0 state (RGBA32F), 1 emission, 2 injection

	void process_pending(); // the queued steps, at the start of a frame
	bool has_volumes() const { return !volumes.is_empty(); }

	// Forward+, per viewport: begin_frame() (the light volumes), apply_shadow() (before the opaque passes), render()
	// (before the transparent pass).
	bool begin_frame(const FrameParams &p_frame);
	bool frame_has_shadows() const;
	void apply_shadow(RID p_mask, bool p_multiply, RID p_depth);
	// p_trace (RGBA16F) and p_trace_depth (R32F): the viewport's half-resolution targets (trace_size()).
	void render(RID p_color, RID p_depth, RID p_trace, RID p_trace_depth);
	Size2i get_trace_size() const { return trace_size; }
	void end_frame() { frame_active = false; }

	FellowGas();
	~FellowGas();
};

} // namespace RendererRD
