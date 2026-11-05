/**************************************************************************/
/*  fellow_shallow_water_3d.h                                             */
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

// FellowEngine GPU shallow-water simulation (river / flood / shallow areas tier of the fluid hierarchy,
// see docs/fellow_fluid_architecture.md).
//
// * Entirely on the GPU: RenderingDevice compute, three RGBA32F state textures (h, u, v, foam) cycled per kernel,
//   a static terrain / obstacle texture and a surface texture (height, gradient, foam) sampled by the water shader.
// * No CPU readback. The CPU uploads only a compact interaction buffer (<= 16 colliders, <= 16 impulses, 768 bytes).
// * Frame-locked: every process step advances exactly `substeps` substeps of `time_step` seconds (deterministic).
// * GPU time of the simulation and of the surface-generation pass is measured with RenderingDevice timestamps.
class FellowShallowWater3D : public Node3D {
	GDCLASS(FellowShallowWater3D, Node3D);

public:
	static constexpr int MAX_COLLIDERS = 16;
	static constexpr int MAX_IMPULSES = 16;

private:
	struct PushConstant {
		int32_t size[2];
		float dx;
		float dt;

		float gravity;
		float damping;
		float solid_height;
		float foam_decay;

		float inflow_velocity[2];
		float inflow_depth;
		int32_t inflow_cells;

		int32_t collider_count;
		int32_t impulse_count;
		float max_speed;
		float collider_drag;
	};
	static_assert(sizeof(PushConstant) == 64);

	struct InteractionBuffer {
		float collider_pos_radius[MAX_COLLIDERS][4];
		float collider_vel[MAX_COLLIDERS][4];
		float impulse[MAX_IMPULSES][4];
	};

	enum Kernel {
		KERNEL_ADVECT,
		KERNEL_HEIGHT,
		KERNEL_VELOCITY,
		KERNEL_SURFACE,
		KERNEL_MAX,
	};

	// Settings.
	int resolution = 512;
	Vector2 domain_size = Vector2(64.0, 64.0);
	int substeps = 2;
	float time_step = 1.0f / 120.0f;
	float gravity = 9.81f;
	float damping = 0.05f;
	float foam_decay = 0.4f;
	float solid_height = 20.0f;
	float collider_drag = 0.25f;
	Vector2 inflow_velocity;
	float inflow_depth = 0.0f;
	int inflow_cells = 2;
	float initial_water_level = 1.0f;
	int render_grid = 512;
	bool simulation_enabled = true;
	Ref<Image> terrain_image;

	// GPU resources (render thread).
	RenderingDevice *rd = nullptr;
	Ref<RDShaderFile> shader_file;
	RID shaders[KERNEL_MAX];
	RID pipelines[KERNEL_MAX];
	RID state[3];
	RID terrain_tex;
	RID surface_tex;
	RID flow_tex; // cell-centre velocity and depth (FellowWater3D)
	RID interaction_buffer;
	RID uniform_sets[KERNEL_MAX];
	int allocated_resolution = 0;
	bool gpu_ready = false;
	bool needs_reset = true;
	bool surface_pending = true; // render thread: the surface pass owed after a reset
	uint64_t timestamps_frame = UINT64_MAX; // main thread: the frame drawn whose step captured timestamps
	bool gpu_failed = false;

	Ref<Texture2DRD> surface_texture;
	Ref<Texture2DRD> terrain_texture;
	Ref<Texture2DRD> flow_texture;

	// Rendering.
	MeshInstance3D *mesh_instance = nullptr;
	Ref<ShaderMaterial> material;
	Ref<Shader> water_shader;
	int mesh_grid = 0;

	// Interaction (CPU side, compact).
	Vector<Vector4> colliders_pos_radius; // x, z in cells, centre height (local m), radius (m)
	Vector<Vector4> colliders_velocity;
	Vector<Vector4> pending_impulses; // x, z in cells, radius in cells, depth change (m)

	// Timing.
	double sim_gpu_ms = -1.0;
	double surface_gpu_ms = -1.0;
	uint64_t steps_simulated = 0;
	uint64_t frames_dispatched = 0;

	float _dx() const { return domain_size.x / float(resolution); }
	Vector2 _world_to_cells(const Vector3 &p_world, float *r_local_y = nullptr) const;

	Ref<Image> _make_terrain(int p_res) const;
	void _update_mesh();
	Ref<ArrayMesh> _build_grid(int n) const;
	void _update_material();

	void _rt_create();
	void _rt_free();
	void _rt_reset();
	void _rt_step(int p_steps, PackedByteArray p_interaction, int p_colliders, int p_impulses, bool p_timestamps);
	void _rt_read_timestamps();
	void _dispatch(RD::ComputeListID p_list, Kernel p_kernel, const PushConstant &p_pc);
	RID _get_uniform_set(Kernel p_kernel, RID p_in, RID p_out);

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;

	void set_resolution(int p_resolution);
	int get_resolution() const { return resolution; }
	void set_domain_size(const Vector2 &p_size);
	Vector2 get_domain_size() const { return domain_size; }
	void set_substeps(int p_substeps);
	int get_substeps() const { return substeps; }
	void set_time_step(float p_dt);
	float get_time_step() const { return time_step; }
	void set_gravity(float p_g) { gravity = p_g; }
	float get_gravity() const { return gravity; }
	void set_damping(float p_d) { damping = p_d; }
	float get_damping() const { return damping; }
	void set_foam_decay(float p_d) { foam_decay = p_d; }
	float get_foam_decay() const { return foam_decay; }
	void set_solid_height(float p_h) { solid_height = p_h; }
	float get_solid_height() const { return solid_height; }
	void set_collider_drag(float p_d) { collider_drag = CLAMP(p_d, 0.0f, 1.0f); }
	float get_collider_drag() const { return collider_drag; }
	void set_inflow_velocity(const Vector2 &p_v) { inflow_velocity = p_v; }
	Vector2 get_inflow_velocity() const { return inflow_velocity; }
	void set_inflow_depth(float p_d) { inflow_depth = p_d; }
	float get_inflow_depth() const { return inflow_depth; }
	void set_inflow_cells(int p_c) { inflow_cells = MAX(p_c, 1); }
	int get_inflow_cells() const { return inflow_cells; }
	void set_initial_water_level(float p_level);
	float get_initial_water_level() const { return initial_water_level; }
	void set_render_grid(int p_grid);
	int get_render_grid() const { return render_grid; }
	void set_simulation_enabled(bool p_enabled) { simulation_enabled = p_enabled; }
	bool is_simulation_enabled() const { return simulation_enabled; }
	void set_terrain_image(const Ref<Image> &p_image);
	Ref<Image> get_terrain_image() const { return terrain_image; }

	void set_collider(int p_index, const Vector3 &p_world_position, float p_radius, const Vector3 &p_world_velocity);
	void clear_colliders();
	void add_impulse(const Vector3 &p_world_position, float p_radius, float p_depth_change);
	void reset_simulation();

	double get_simulation_gpu_time_ms() const { return sim_gpu_ms; }
	double get_surface_gpu_time_ms() const { return surface_gpu_ms; }
	int64_t get_simulation_vram_bytes() const;
	int64_t get_steps_simulated() const { return steps_simulated; }
	Ref<Texture2D> get_surface_texture() const { return surface_texture; }
	Ref<Texture2D> get_terrain_texture() const { return terrain_texture; }
	Ref<Texture2D> get_flow_texture() const { return flow_texture; }
	MeshInstance3D *get_water_mesh_instance() const { return mesh_instance; }

	FellowShallowWater3D();
	~FellowShallowWater3D();
};
