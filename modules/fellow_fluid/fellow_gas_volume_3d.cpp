/**************************************************************************/
/*  fellow_gas_volume_3d.cpp                                              */
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

#include "fellow_gas_volume_3d.h"

#include "fellow_burnable_3d.h"
#include "fellow_editor_still.h"
#include "fellow_gas_emitter_3d.h"
#include "fellow_node_warnings.h"
#include "fellow_wind_3d.h"

#include "core/config/engine.h"
#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "servers/physics_3d/physics_server_3d.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/renderer_rd/effects/fellow_gas.h"
#include "servers/rendering/rendering_server.h"

using FellowGas = RendererRD::FellowGas;

FellowGasVolume3D::FellowGasVolume3D() {
	volume_id = FellowGas::allocate_id();
	emission.instantiate();
	debug.instantiate();
	velocity_texture.instantiate();
	_update_grid();
}

FellowGasVolume3D::~FellowGasVolume3D() {
	if (query_shape.is_valid() && PhysicsServer3D::get_singleton()) {
		PhysicsServer3D::get_singleton()->free_rid(query_shape);
	}
}

// ------------------------------------------------------------------ grid

void FellowGasVolume3D::_update_grid() {
	const float longest = MAX(size.x, MAX(size.y, size.z));
	grid_dx = longest / float(resolution);
	grid_res = Vector3i(MAX(4, int(Math::round(size.x / grid_dx))), MAX(4, int(Math::round(size.y / grid_dx))), MAX(4, int(Math::round(size.z / grid_dx))));
}

Transform3D FellowGasVolume3D::_grid_to_world() const {
	if (space == SPACE_WORLD) {
		return Transform3D(Basis::from_scale(Vector3(grid_dx, grid_dx, grid_dx)), grid_origin);
	}
	const Transform3D local(Basis::from_scale(Vector3(grid_dx, grid_dx, grid_dx)), Vector3(-grid_res.x * grid_dx * 0.5f, 0.0f, -grid_res.z * grid_dx * 0.5f));
	return (is_inside_tree() ? get_global_transform() : get_transform()) * local;
}

Basis FellowGasVolume3D::_world_to_grid_axes() const {
	if (space == SPACE_WORLD) {
		return Basis();
	}
	return (is_inside_tree() ? get_global_transform() : get_transform()).basis.inverse();
}

Vector3 FellowGasVolume3D::_snapped_origin() const {
	const Vector3 p = (is_inside_tree() ? get_global_transform() : get_transform()).origin;
	const Vector3 corner = p - Vector3(grid_res.x * grid_dx * 0.5f, 0.0f, grid_res.z * grid_dx * 0.5f);
	return Vector3(Math::round(corner.x / grid_dx), Math::round(corner.y / grid_dx), Math::round(corner.z / grid_dx)) * grid_dx;
}

void FellowGasVolume3D::set_space(Space p_space) {
	space = p_space;
	configured = false;
}

void FellowGasVolume3D::set_size(const Vector3 &p_size) {
	ERR_FAIL_COND(p_size.x <= 0.0f || p_size.y <= 0.0f || p_size.z <= 0.0f);
	size = p_size;
	_update_grid();
	configured = false;
	update_gizmos();
	for (int i = 0; i < get_child_count(); i++) {
		if (Node *e = Object::cast_to<FellowGasEmitter3D>(get_child(i))) {
			e->update_configuration_warnings(); // inside the box or not (milestone 54)
		}
	}
}

void FellowGasVolume3D::set_resolution(int p_resolution) {
	ERR_FAIL_COND_MSG(p_resolution < 8 || p_resolution > 512, "Resolution must be in [8, 512].");
	resolution = p_resolution;
	_update_grid();
	configured = false;
}

void FellowGasVolume3D::set_high_precision(bool p_enable) {
	high_precision = p_enable;
	configured = false;
}

void FellowGasVolume3D::set_fire_light(bool p_enable) {
	fire_light = p_enable;
	if (light && !fire_light) {
		light->set_visible(false);
	}
}

void FellowGasVolume3D::set_velocity_texture_enabled(bool p_enable) {
	velocity_texture_enabled = p_enable;
	if (configured) {
		RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowGasVolume3D::_rt_export).bind(volume_id, p_enable, velocity_texture_scale, velocity_texture));
	}
}

void FellowGasVolume3D::set_velocity_texture_scale(float p_scale) {
	velocity_texture_scale = MAX(p_scale, 0.01f);
	if (configured && velocity_texture_enabled) {
		RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowGasVolume3D::_rt_export).bind(volume_id, true, velocity_texture_scale, velocity_texture));
	}
}

int64_t FellowGasVolume3D::get_vram_bytes() const {
	return FellowGas::estimate_vram_bytes(grid_res, high_precision);
}

void FellowGasVolume3D::reset_simulation() {
	ticks = 0;
	sim_time = 0.0;
	prewarm_left = prewarm_steps;
	if (configured) {
		RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowGasVolume3D::_rt_reset).bind(volume_id));
	}
}

// ------------------------------------------------------------------ render thread

void FellowGasVolume3D::_rt_configure(uint32_t p_id, Vector3i p_res, float p_dx, bool p_high_precision, Ref<FellowGasReadback> p_emission) {
	FellowGas *gas = FellowGas::get_singleton();
	if (!gas) {
		WARN_PRINT_ONCE("FellowGasVolume3D needs the Forward+ renderer: with the Compatibility renderer it is neither simulated nor drawn.");
		return;
	}
	if (RS::get_singleton()->get_current_rendering_method() != "forward_plus") {
		// Decision 0150: the gas is drawn in the clustered renderer's passes only (its simulation runs everywhere).
		WARN_PRINT_ONCE("FellowGasVolume3D is drawn by the Forward+ renderer only: with the Mobile renderer it is simulated but not drawn.");
	}
	gas->volume_configure(p_id, p_res, p_dx, p_high_precision);
	gas->volume_set_emission_callback(p_id, callable_mp_static(&FellowGasVolume3D::_rt_store).bind(p_emission));
}

void FellowGasVolume3D::_rt_export(uint32_t p_id, bool p_enable, float p_scale, Ref<Texture3DRD> p_texture) {
	FellowGas *gas = FellowGas::get_singleton_if_exists();
	if (!gas || p_texture.is_null()) {
		return;
	}
	p_texture->set_texture_rd_rid(gas->volume_enable_velocity_export(p_id, p_enable, p_scale));
}

void FellowGasVolume3D::_rt_free(uint32_t p_id) {
	if (FellowGas *gas = FellowGas::get_singleton_if_exists()) {
		gas->volume_free(p_id);
	}
}

void FellowGasVolume3D::_rt_reset(uint32_t p_id) {
	if (FellowGas *gas = FellowGas::get_singleton_if_exists()) {
		gas->volume_reset(p_id);
	}
}

void FellowGasVolume3D::_rt_step(uint32_t p_id, PackedByteArray p_step) {
	FellowGas *gas = FellowGas::get_singleton_if_exists();
	if (!gas || p_step.size() != int64_t(sizeof(FellowGas::StepParams))) {
		return;
	}
	FellowGas::StepParams sp;
	memcpy(&sp, p_step.ptr(), sizeof(FellowGas::StepParams));
	gas->volume_queue_step(p_id, sp);
}

void FellowGasVolume3D::_rt_render(uint32_t p_id, PackedByteArray p_render) {
	FellowGas *gas = FellowGas::get_singleton_if_exists();
	if (!gas || p_render.size() != int64_t(sizeof(FellowGas::RenderParams))) {
		return;
	}
	FellowGas::RenderParams rp;
	memcpy(&rp, p_render.ptr(), sizeof(FellowGas::RenderParams));
	gas->volume_set_render(p_id, rp);
}

void FellowGasVolume3D::_rt_debug(uint32_t p_id, int p_what, Ref<FellowGasReadback> p_target) {
	if (FellowGas *gas = FellowGas::get_singleton_if_exists()) {
		gas->volume_debug_read(p_id, p_what, callable_mp_static(&FellowGasVolume3D::_rt_store).bind(p_target));
	}
}

void FellowGasVolume3D::_rt_store(const PackedByteArray &p_data, Ref<FellowGasReadback> p_target) {
	if (p_target.is_valid()) {
		p_target->store(p_data);
	}
}

// ------------------------------------------------------------------ main thread

void FellowGasVolume3D::_configure() {
	_update_grid();
	grid_origin = _snapped_origin();
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowGasVolume3D::_rt_configure).bind(volume_id, grid_res, grid_dx, high_precision, emission));
	if (velocity_texture_enabled) {
		RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowGasVolume3D::_rt_export).bind(volume_id, true, velocity_texture_scale, velocity_texture));
	}
	configured = true;
	ticks = 0;
	sim_time = 0.0;
	prewarm_left = prewarm_steps;
}

// Grid-space axes of a node's basis: unit rows (grid -> the node's local axes) and the grid cells per local unit along
// each axis.
static void _fellow_gas_axes(const Basis &p_grid_basis, float r_rot[3][4], Vector3 &r_cells_per_unit) {
	for (int i = 0; i < 3; i++) {
		Vector3 c = p_grid_basis.get_column(i);
		const float l = c.length();
		r_cells_per_unit[i] = l;
		c = l > 1e-8f ? c / l : Vector3(i == 0, i == 1, i == 2);
		r_rot[i][0] = c.x;
		r_rot[i][1] = c.y;
		r_rot[i][2] = c.z;
	}
}

void FellowGasVolume3D::_gather_emitters(void *r_step) const {
	FellowGas::StepParams &sp = *reinterpret_cast<FellowGas::StepParams *>(r_step);
	const Transform3D w2g = _grid_to_world().affine_inverse();
	const Basis node_inv = _world_to_grid_axes();
	int count = 0;
	for (int i = 0; i < get_child_count() && count < FellowGas::MAX_EMITTERS; i++) {
		FellowGasEmitter3D *e = Object::cast_to<FellowGasEmitter3D>(get_child(i));
		if (!e || !e->is_emitting() || !e->is_visible_in_tree()) {
			continue;
		}
		FellowGas::GPUEmitter &g = sp.emitters[count];
		memset(&g, 0, sizeof(FellowGas::GPUEmitter));
		const Transform3D et = e->get_global_transform();
		const Vector3 c = w2g.xform(et.origin);
		float rot[3][4];
		Vector3 cpu;
		_fellow_gas_axes(w2g.basis * et.basis, rot, cpu);
		g.centre_radius[0] = c.x;
		g.centre_radius[1] = c.y;
		g.centre_radius[2] = c.z;
		float *rows[3] = { g.rot0, g.rot1, g.rot2 };
		for (int r = 0; r < 3; r++) {
			rows[r][0] = rot[r][0];
			rows[r][1] = rot[r][1];
			rows[r][2] = rot[r][2];
		}
		const float mean = (cpu.x + cpu.y + cpu.z) / 3.0f;
		switch (e->get_shape()) {
			case FellowGasEmitter3D::SHAPE_BOX: {
				const Vector3 h = e->get_box_size() * 0.5f;
				g.rot0[3] = h.x * cpu.x;
				g.rot1[3] = h.y * cpu.y;
				g.rot2[3] = h.z * cpu.z;
				g.shape[0] = 1.0f;
			} break;
			case FellowGasEmitter3D::SHAPE_CYLINDER: {
				g.centre_radius[3] = e->get_radius() * 0.5f * (cpu.x + cpu.z);
				g.rot0[3] = e->get_height() * 0.5f * cpu.y;
				g.shape[0] = 2.0f;
			} break;
			default: {
				g.centre_radius[3] = e->get_radius() * mean;
				g.shape[0] = 0.0f;
			} break;
		}
		g.rates[0] = e->get_smoke();
		g.rates[1] = e->get_temperature();
		g.rates[2] = e->get_fuel();
		g.rates[3] = e->get_noise();
		const Vector3 v = node_inv.xform(et.basis.orthonormalized().xform(e->get_velocity()));
		g.velocity[0] = v.x;
		g.velocity[1] = v.y;
		g.velocity[2] = v.z;
		g.velocity[3] = e->get_velocity_strength();
		g.shape[1] = 1.0f / MAX(e->get_noise_scale() * mean, 1e-3f);
		g.shape[2] = e->get_temperature_mode() == FellowGasEmitter3D::TEMPERATURE_ADD ? 1.0f : 0.0f;
		count++;
	}
	sp.emitter_count = count;
}

void FellowGasVolume3D::_gather_obstacles(void *r_step) const {
	FellowGas::StepParams &sp = *reinterpret_cast<FellowGas::StepParams *>(r_step);
	sp.obstacle_count = 0;
	if (!physics_obstacles || !is_inside_tree() || query_shape.is_null()) {
		return;
	}
	Ref<World3D> world = get_world_3d();
	if (world.is_null()) {
		return;
	}
	PhysicsDirectSpaceState3D *space_state = world->get_direct_space_state();
	if (!space_state) {
		return;
	}
	PhysicsServer3D *ps = PhysicsServer3D::get_singleton();
	const Transform3D g2w = _grid_to_world();
	const Transform3D w2g = g2w.affine_inverse();
	AABB box;
	for (int c = 0; c < 8; c++) {
		const Vector3 g((c & 1) ? grid_res.x : 0, (c & 2) ? grid_res.y : 0, (c & 4) ? grid_res.z : 0);
		const Vector3 w = g2w.xform(g);
		if (c == 0) {
			box = AABB(w, Vector3());
		} else {
			box.expand_to(w);
		}
	}
	ps->shape_set_data(query_shape, box.size * 0.5f);
	PhysicsDirectSpaceState3D::ShapeParameters params;
	params.shape_rid = query_shape;
	params.transform = Transform3D(Basis(), box.get_center());
	params.collision_mask = collision_mask;
	params.collide_with_bodies = true;
	params.collide_with_areas = false;
	PhysicsDirectSpaceState3D::ShapeResult results[32];
	const int n = space_state->intersect_shape(params, results, 32);
	struct Found {
		float distance;
		uint64_t object;
		int shape;
		FellowGas::GPUObstacle gpu;
		bool operator<(const Found &p_o) const { return distance < p_o.distance || (distance == p_o.distance && (object < p_o.object || (object == p_o.object && shape < p_o.shape))); }
	};
	LocalVector<Found> found;
	const Basis node_inv = _world_to_grid_axes();
	const Basis node_rot_inv = this->space == SPACE_WORLD ? Basis() : get_global_transform().basis.orthonormalized().inverse();
	const Vector3 centre_w = box.get_center();
	const float max_speed = 6.0f * grid_dx / MAX(float(get_physics_process_delta_time() * time_scale / substeps), 1e-5f); // FellowGas::StepParams::max_speed_cells
	for (int i = 0; i < n; i++) {
		const RID body = results[i].rid;
		const int shape_idx = results[i].shape;
		if (body.is_null() || shape_idx < 0 || shape_idx >= ps->body_get_shape_count(body)) {
			continue;
		}
		if (FellowBurnable3D::is_burnable_body(results[i].collider_id)) {
			continue; // a burning object is its own obstacle (its voxels' solid, decision 0120)
		}
		const RID shape = ps->body_get_shape(body, shape_idx);
		const PhysicsServer3D::ShapeType type = ps->shape_get_type(shape);
		const Variant shape_data = ps->shape_get_data(shape);
		const Transform3D body_xform = ps->body_get_state(body, PhysicsServer3D::BODY_STATE_TRANSFORM);
		Transform3D xform = body_xform * ps->body_get_shape_transform(body, shape_idx);
		Found f;
		memset(&f.gpu, 0, sizeof(FellowGas::GPUObstacle));
		float rot[3][4];
		Vector3 cpu;
		switch (type) {
			case PhysicsServer3D::SHAPE_SPHERE: {
				_fellow_gas_axes(w2g.basis * xform.basis, rot, cpu);
				f.gpu.centre_type[3] = 0.0f;
				f.gpu.rot0[3] = float(shape_data) * (cpu.x + cpu.y + cpu.z) / 3.0f;
			} break;
			case PhysicsServer3D::SHAPE_BOX: {
				_fellow_gas_axes(w2g.basis * xform.basis, rot, cpu);
				const Vector3 h = shape_data;
				f.gpu.centre_type[3] = 1.0f;
				f.gpu.rot0[3] = h.x * cpu.x;
				f.gpu.rot1[3] = h.y * cpu.y;
				f.gpu.rot2[3] = h.z * cpu.z;
			} break;
			case PhysicsServer3D::SHAPE_CAPSULE:
			case PhysicsServer3D::SHAPE_CYLINDER: {
				_fellow_gas_axes(w2g.basis * xform.basis, rot, cpu);
				const Dictionary d = shape_data;
				const float r = d.get("radius", 0.5);
				const float h = d.get("height", 1.0);
				f.gpu.rot0[3] = r * 0.5f * (cpu.x + cpu.z);
				if (type == PhysicsServer3D::SHAPE_CAPSULE) {
					f.gpu.centre_type[3] = 2.0f;
					f.gpu.rot1[3] = MAX(h * 0.5f - r, 0.0f) * cpu.y;
				} else {
					f.gpu.centre_type[3] = 3.0f;
					f.gpu.rot1[3] = h * 0.5f * cpu.y;
				}
			} break;
			case PhysicsServer3D::SHAPE_CONVEX_POLYGON: {
				// Its box (in the shape's space).
				const PackedVector3Array pts = shape_data;
				if (pts.is_empty()) {
					continue;
				}
				AABB a(pts[0], Vector3());
				for (const Vector3 &p : pts) {
					a.expand_to(p);
				}
				xform = xform * Transform3D(Basis(), a.get_center());
				_fellow_gas_axes(w2g.basis * xform.basis, rot, cpu);
				f.gpu.centre_type[3] = 1.0f;
				f.gpu.rot0[3] = a.size.x * 0.5f * cpu.x;
				f.gpu.rot1[3] = a.size.y * 0.5f * cpu.y;
				f.gpu.rot2[3] = a.size.z * 0.5f * cpu.z;
			} break;
			case PhysicsServer3D::SHAPE_WORLD_BOUNDARY: {
				// Solid below the plane: a frame on the plane with its y axis the plane's normal.
				const Plane p = shape_data;
				Basis b;
				b.set_column(1, p.normal);
				b.set_column(0, p.normal.cross(Math::abs(p.normal.x) < 0.9f ? Vector3(1, 0, 0) : Vector3(0, 0, 1)).normalized());
				b.set_column(2, b.get_column(0).cross(p.normal));
				xform = xform * Transform3D(b, p.normal * p.d);
				_fellow_gas_axes(w2g.basis * xform.basis, rot, cpu);
				f.gpu.centre_type[3] = 4.0f;
			} break;
			default:
				continue; // concave and height map shapes are not obstacles of the gas (close the volume's floor)
		}
		const Vector3 c = w2g.xform(xform.origin);
		f.gpu.centre_type[0] = c.x;
		f.gpu.centre_type[1] = c.y;
		f.gpu.centre_type[2] = c.z;
		float *rows[3] = { f.gpu.rot0, f.gpu.rot1, f.gpu.rot2 };
		for (int r = 0; r < 3; r++) {
			rows[r][0] = rot[r][0];
			rows[r][1] = rot[r][1];
			rows[r][2] = rot[r][2];
		}
		const Vector3 lin = ps->body_get_state(body, PhysicsServer3D::BODY_STATE_LINEAR_VELOCITY);
		const Vector3 ang = ps->body_get_state(body, PhysicsServer3D::BODY_STATE_ANGULAR_VELOCITY);
		Vector3 v = node_inv.xform(lin + ang.cross(xform.origin - body_xform.origin));
		Vector3 w = node_rot_inv.xform(ang);
		// A body moved faster than the grid can carry (six cells per step) was teleported (a kinematic body's
		// position set: the physics server reports the jump as one step's velocity): it does not push the gas.
		const float extent = MAX(f.gpu.rot0[3], MAX(f.gpu.rot1[3], f.gpu.rot2[3])) * grid_dx;
		if (v.length() > max_speed || w.length() * extent > max_speed) {
			v = Vector3();
			w = Vector3();
		}
		f.gpu.linear_velocity[0] = v.x;
		f.gpu.linear_velocity[1] = v.y;
		f.gpu.linear_velocity[2] = v.z;
		f.gpu.angular_velocity[0] = w.x;
		f.gpu.angular_velocity[1] = w.y;
		f.gpu.angular_velocity[2] = w.z;
		f.distance = type == PhysicsServer3D::SHAPE_WORLD_BOUNDARY ? 0.0f : (xform.origin - centre_w).length();
		f.object = results[i].collider_id;
		f.shape = shape_idx;
		found.push_back(f);
	}
	found.sort();
	const int count = MIN(int(found.size()), FellowGas::MAX_OBSTACLES);
	for (int i = 0; i < count; i++) {
		sp.obstacles[i] = found[i].gpu;
	}
	sp.obstacle_count = count;
}

void FellowGasVolume3D::_queue_step(bool p_read_emission) {
	FellowGas::StepParams sp;
	memset(sp.emitters, 0, sizeof(sp.emitters));
	memset(sp.obstacles, 0, sizeof(sp.obstacles));
	const double tick = get_physics_process_delta_time();
	sp.dt = float(tick * time_scale / substeps);
	if (space == SPACE_WORLD) {
		// The box follows the node by whole cells; the state shifts with it.
		const Vector3 o = _snapped_origin();
		const Vector3 d = (o - grid_origin) / grid_dx;
		sp.shift[0] = int(Math::round(d.x));
		sp.shift[1] = int(Math::round(d.y));
		sp.shift[2] = int(Math::round(d.z));
		grid_origin = o;
	}
	const Basis to_grid = _world_to_grid_axes();
	const Vector3 up = to_grid.xform(Vector3(0, 1, 0)).normalized();
	sp.up[0] = up.x;
	sp.up[1] = up.y;
	sp.up[2] = up.z;
	sp.buoyancy = buoyancy;
	Vector3 wind_now = wind;
	if (wind_from_scene) {
		FellowWind3D *scene_wind = FellowWind3D::get_active();
		if (scene_wind && scene_wind->is_visible_in_tree()) {
			wind_now = scene_wind->get_velocity_at(get_global_transform().xform(Vector3(0.0f, size.y * 0.5f, 0.0f)));
		}
	}
	const Vector3 w = to_grid.xform(wind_now);
	sp.wind[0] = w.x;
	sp.wind[1] = w.y;
	sp.wind[2] = w.z;
	sp.wind_rate = wind_rate;
	sp.smoke_weight = smoke_weight;
	sp.vorticity = vorticity;
	sp.turbulence = turbulence;
	sp.turbulence_frequency = grid_dx / turbulence_scale;
	sp.smoke_dissipation = smoke_dissipation;
	sp.cooling = cooling;
	sp.ignition = ignition_temperature;
	sp.flame_limit = flame_temperature_limit;
	sp.burn_rate = burn_rate;
	sp.heat = heat_release;
	sp.soot = soot;
	sp.expansion = expansion;
	sp.omega = 1.6f;
	sp.time = float(sim_time);
	sp.emission_intensity = fire_intensity;
	sp.emission_temperature_scale = fire_temperature_scale;
	sp.iterations = pressure_iterations;
	sp.multigrid = pressure_solver == SOLVER_MULTIGRID ? 1 : 0;
	sp.multigrid_cycles = multigrid_cycles;
	sp.sparse = sparse ? 1 : 0;
	sp.sparse_margin = sparse_margin;
	sp.closed_faces = closed_faces;
	sp.maccormack = maccormack ? 1 : 0;
	sp.read_emission = p_read_emission ? 1 : 0;
	sp.tick = Engine::get_singleton()->get_physics_frames();
	_gather_emitters(&sp);
	_gather_obstacles(&sp);
	PackedByteArray bytes;
	bytes.resize(sizeof(FellowGas::StepParams));
	memcpy(bytes.ptrw(), &sp, sizeof(FellowGas::StepParams));
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowGasVolume3D::_rt_step).bind(volume_id, bytes));
	sim_time += sp.dt;
	ticks++;
}

void FellowGasVolume3D::_update_fire_light() {
	PackedByteArray light_data;
	{
		MutexLock lock(emission->mutex);
		if (!emission->fresh) {
			return;
		}
		light_data = emission->data;
		emission->fresh = false;
	}
	const float *f = reinterpret_cast<const float *>(light_data.ptr());
	const int groups = int(light_data.size() / 32);
	// Summed in a fixed order: the same light on every run.
	double l = 0.0;
	Vector3 p;
	double rgb[3] = { 0.0, 0.0, 0.0 };
	for (int i = 0; i < groups; i++) {
		l += f[i * 8 + 0];
		p += Vector3(f[i * 8 + 1], f[i * 8 + 2], f[i * 8 + 3]);
		rgb[0] += f[i * 8 + 4];
		rgb[1] += f[i * 8 + 5];
		rgb[2] += f[i * 8 + 6];
	}
	fire_power = float(l);
	if (l > 1e-6) {
		fire_light_position = _grid_to_world().xform(p / float(l));
		fire_light_rgb = Color(rgb[0], rgb[1], rgb[2]);
	} else {
		fire_light_rgb = Color(0, 0, 0);
	}
	if (!light) {
		return;
	}
	const float m = MAX(fire_light_rgb.r, MAX(fire_light_rgb.g, fire_light_rgb.b));
	const bool on = fire_light && m > 1e-5f && is_visible_in_tree();
	light->set_visible(on);
	if (!on) {
		return;
	}
	// A point source of the fire's radiant intensity: the omni light's energy is it / pi (Lambert surfaces), with
	// inverse-square attenuation.
	const float energy = m / Math::PI * fire_light_energy;
	light->set_color(Color(fire_light_rgb.r / m, fire_light_rgb.g / m, fire_light_rgb.b / m));
	light->set_param(Light3D::PARAM_ENERGY, energy);
	light->set_param(Light3D::PARAM_ATTENUATION, 2.0f);
	const float range = fire_light_range > 0.0f ? fire_light_range : CLAMP(Math::sqrt(energy / 0.004f), 2.0f, 60.0f);
	light->set_param(Light3D::PARAM_RANGE, range);
	light->set_shadow(fire_light_shadows);
	light->set_global_position(fire_light_position);
}

void FellowGasVolume3D::_send_render() {
	FellowGas::RenderParams rp;
	rp.grid_to_world = _grid_to_world();
	const Color c = smoke_color.srgb_to_linear();
	rp.albedo[0] = c.r;
	rp.albedo[1] = c.g;
	rp.albedo[2] = c.b;
	rp.density = smoke_density;
	rp.emission_intensity = fire_intensity;
	rp.emission_temperature_scale = fire_temperature_scale;
	rp.anisotropy = anisotropy;
	rp.ambient = ambient_strength;
	rp.self_shadow = self_shadow;
	rp.scene_shadow = shadow_strength;
	rp.step_cells = render_step;
	rp.fire_light_position[0] = fire_light_position.x;
	rp.fire_light_position[1] = fire_light_position.y;
	rp.fire_light_position[2] = fire_light_position.z;
	rp.fire_light_intensity[0] = fire_light_rgb.r;
	rp.fire_light_intensity[1] = fire_light_rgb.g;
	rp.fire_light_intensity[2] = fire_light_rgb.b;
	rp.fire_light = (fire_light && fire_power > 0.0f) ? 1 : 0;
	rp.casts_shadows = cast_shadows ? 1 : 0;
	rp.visible = (render_enabled && is_visible_in_tree()) ? 1 : 0;
	PackedByteArray bytes;
	bytes.resize(sizeof(FellowGas::RenderParams));
	memcpy(bytes.ptrw(), &rp, sizeof(FellowGas::RenderParams));
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowGasVolume3D::_rt_render).bind(volume_id, bytes));
}

void FellowGasVolume3D::request_debug_data(int p_what) {
	{
		MutexLock lock(debug->mutex);
		debug->fresh = false;
	}
	RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowGasVolume3D::_rt_debug).bind(volume_id, p_what, debug));
}

bool FellowGasVolume3D::is_debug_data_ready() const {
	MutexLock lock(debug->mutex);
	return debug->fresh;
}

PackedByteArray FellowGasVolume3D::get_debug_data() const {
	MutexLock lock(debug->mutex);
	return debug->data;
}

PackedStringArray FellowGasVolume3D::get_configuration_warnings() const {
	PackedStringArray warnings = Node3D::get_configuration_warnings();
	if (FellowWarnings::fluids_off(warnings)) {
		return warnings;
	}
	if (FellowWarnings::compatibility()) {
		warnings.push_back(RTR("The Compatibility renderer neither simulates nor draws gas: use Forward+."));
	} else if (FellowWarnings::mobile()) {
		warnings.push_back(RTR("The Mobile renderer simulates the gas but does not draw it: use Forward+."));
	}
	bool emitter = false;
	for (int i = 0; i < get_child_count() && !emitter; i++) {
		emitter = Object::cast_to<FellowGasEmitter3D>(get_child(i)) != nullptr;
	}
	if (!emitter && is_inside_tree()) {
		// A burning object fills a volume around it without emitters.
		const Node *root = FellowWarnings::scene_root(this);
		if (!root || root->find_children("*", "FellowBurnable3D", true, false).is_empty()) {
			warnings.push_back(RTR("Nothing fills this volume: add a FellowGasEmitter3D as its child (or a FellowBurnable3D inside it)."));
		}
	}
	return warnings;
}

void FellowGasVolume3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_CHILD_ORDER_CHANGED: {
			update_configuration_warnings();
		} break;
		case NOTIFICATION_ENTER_TREE: {
			add_to_group(SNAME("fellow_gas_volumes"));
			if (query_shape.is_null() && PhysicsServer3D::get_singleton()) {
				query_shape = PhysicsServer3D::get_singleton()->box_shape_create();
			}
			if (!light) {
				light = memnew(OmniLight3D);
				light->set_name("FellowFireLight");
				light->set_visible(false);
				light->set_bake_mode(Light3D::BAKE_DISABLED);
				add_child(light, false, INTERNAL_MODE_FRONT);
			}
			set_physics_process_internal(true);
			set_process_internal(true);
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (configured) {
				RS::get_singleton()->call_on_render_thread(callable_mp_static(&FellowGasVolume3D::_rt_free).bind(volume_id));
				configured = false;
			}
		} break;
		case NOTIFICATION_INTERNAL_PHYSICS_PROCESS: {
			if (!Fellow::fluid_enabled()) {
				return;
			}
			if (!configured) {
				_configure();
			}
			if (!simulation_enabled) {
				return;
			}
			// Prewarm in batches (the GPU queue holds 64 steps), then one tick's steps.
			const int batch = MIN(prewarm_left, 32);
			for (int i = 0; i < batch; i++) {
				_queue_step(false);
			}
			prewarm_left -= batch;
			// Still in the editor with its Animate Nodes toggle off (milestone 52): prewarmed, then not stepped.
			if (fellow_editor_still()) {
				return;
			}
			for (int i = 0; i < substeps; i++) {
				_queue_step(i == substeps - 1 && fire_light);
			}
			// The gas moved: drawn (milestone 52; the editor draws only on a change) — while something emits or burns,
			// and the 20 s after, as the smoke clears; an empty box asks for nothing.
			bool emitting = fire_power > 0.0f;
			for (int i = 0; i < get_child_count() && !emitting; i++) {
				const FellowGasEmitter3D *e = Object::cast_to<FellowGasEmitter3D>(get_child(i));
				emitting = e && e->is_emitting() && e->is_visible_in_tree();
			}
			if (emitting) {
				redraw_ticks = 20 * Engine::get_singleton()->get_physics_ticks_per_second();
			}
			if (substeps > 0 && render_enabled && is_visible_in_tree() && redraw_ticks > 0) {
				redraw_ticks--;
				Fellow::request_redraw();
			}
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!configured) {
				return;
			}
			_update_fire_light();
			_send_render();
		} break;
	}
}

void FellowGasVolume3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_size", "size"), &FellowGasVolume3D::set_size);
	ClassDB::bind_method(D_METHOD("get_size"), &FellowGasVolume3D::get_size);
	ClassDB::bind_method(D_METHOD("set_resolution", "cells"), &FellowGasVolume3D::set_resolution);
	ClassDB::bind_method(D_METHOD("get_resolution"), &FellowGasVolume3D::get_resolution);
	ClassDB::bind_method(D_METHOD("set_high_precision", "enable"), &FellowGasVolume3D::set_high_precision);
	ClassDB::bind_method(D_METHOD("is_high_precision"), &FellowGasVolume3D::is_high_precision);
	ClassDB::bind_method(D_METHOD("set_space", "space"), &FellowGasVolume3D::set_space);
	ClassDB::bind_method(D_METHOD("get_space"), &FellowGasVolume3D::get_space);
	ClassDB::bind_method(D_METHOD("set_simulation_enabled", "enable"), &FellowGasVolume3D::set_simulation_enabled);
	ClassDB::bind_method(D_METHOD("is_simulation_enabled"), &FellowGasVolume3D::is_simulation_enabled);
	ClassDB::bind_method(D_METHOD("set_substeps", "substeps"), &FellowGasVolume3D::set_substeps);
	ClassDB::bind_method(D_METHOD("get_substeps"), &FellowGasVolume3D::get_substeps);
	ClassDB::bind_method(D_METHOD("set_time_scale", "scale"), &FellowGasVolume3D::set_time_scale);
	ClassDB::bind_method(D_METHOD("get_time_scale"), &FellowGasVolume3D::get_time_scale);
	ClassDB::bind_method(D_METHOD("set_prewarm_steps", "steps"), &FellowGasVolume3D::set_prewarm_steps);
	ClassDB::bind_method(D_METHOD("get_prewarm_steps"), &FellowGasVolume3D::get_prewarm_steps);
	ClassDB::bind_method(D_METHOD("set_closed_faces", "faces"), &FellowGasVolume3D::set_closed_faces);
	ClassDB::bind_method(D_METHOD("get_closed_faces"), &FellowGasVolume3D::get_closed_faces);
	ClassDB::bind_method(D_METHOD("set_buoyancy", "buoyancy"), &FellowGasVolume3D::set_buoyancy);
	ClassDB::bind_method(D_METHOD("get_buoyancy"), &FellowGasVolume3D::get_buoyancy);
	ClassDB::bind_method(D_METHOD("set_smoke_weight", "weight"), &FellowGasVolume3D::set_smoke_weight);
	ClassDB::bind_method(D_METHOD("get_smoke_weight"), &FellowGasVolume3D::get_smoke_weight);
	ClassDB::bind_method(D_METHOD("set_vorticity", "vorticity"), &FellowGasVolume3D::set_vorticity);
	ClassDB::bind_method(D_METHOD("get_vorticity"), &FellowGasVolume3D::get_vorticity);
	ClassDB::bind_method(D_METHOD("set_turbulence", "acceleration"), &FellowGasVolume3D::set_turbulence);
	ClassDB::bind_method(D_METHOD("get_turbulence"), &FellowGasVolume3D::get_turbulence);
	ClassDB::bind_method(D_METHOD("set_turbulence_scale", "metres"), &FellowGasVolume3D::set_turbulence_scale);
	ClassDB::bind_method(D_METHOD("get_turbulence_scale"), &FellowGasVolume3D::get_turbulence_scale);
	ClassDB::bind_method(D_METHOD("set_wind", "wind"), &FellowGasVolume3D::set_wind);
	ClassDB::bind_method(D_METHOD("get_wind"), &FellowGasVolume3D::get_wind);
	ClassDB::bind_method(D_METHOD("set_wind_rate", "rate"), &FellowGasVolume3D::set_wind_rate);
	ClassDB::bind_method(D_METHOD("get_wind_rate"), &FellowGasVolume3D::get_wind_rate);
	ClassDB::bind_method(D_METHOD("set_wind_from_scene", "enable"), &FellowGasVolume3D::set_wind_from_scene);
	ClassDB::bind_method(D_METHOD("is_wind_from_scene"), &FellowGasVolume3D::is_wind_from_scene);
	ClassDB::bind_method(D_METHOD("set_smoke_dissipation", "rate"), &FellowGasVolume3D::set_smoke_dissipation);
	ClassDB::bind_method(D_METHOD("get_smoke_dissipation"), &FellowGasVolume3D::get_smoke_dissipation);
	ClassDB::bind_method(D_METHOD("set_cooling", "rate"), &FellowGasVolume3D::set_cooling);
	ClassDB::bind_method(D_METHOD("get_cooling"), &FellowGasVolume3D::get_cooling);
	ClassDB::bind_method(D_METHOD("set_ignition_temperature", "kelvin"), &FellowGasVolume3D::set_ignition_temperature);
	ClassDB::bind_method(D_METHOD("get_ignition_temperature"), &FellowGasVolume3D::get_ignition_temperature);
	ClassDB::bind_method(D_METHOD("set_flame_temperature_limit", "kelvin"), &FellowGasVolume3D::set_flame_temperature_limit);
	ClassDB::bind_method(D_METHOD("get_flame_temperature_limit"), &FellowGasVolume3D::get_flame_temperature_limit);
	ClassDB::bind_method(D_METHOD("set_burn_rate", "rate"), &FellowGasVolume3D::set_burn_rate);
	ClassDB::bind_method(D_METHOD("get_burn_rate"), &FellowGasVolume3D::get_burn_rate);
	ClassDB::bind_method(D_METHOD("set_heat_release", "kelvin"), &FellowGasVolume3D::set_heat_release);
	ClassDB::bind_method(D_METHOD("get_heat_release"), &FellowGasVolume3D::get_heat_release);
	ClassDB::bind_method(D_METHOD("set_soot", "soot"), &FellowGasVolume3D::set_soot);
	ClassDB::bind_method(D_METHOD("get_soot"), &FellowGasVolume3D::get_soot);
	ClassDB::bind_method(D_METHOD("set_expansion", "expansion"), &FellowGasVolume3D::set_expansion);
	ClassDB::bind_method(D_METHOD("get_expansion"), &FellowGasVolume3D::get_expansion);
	ClassDB::bind_method(D_METHOD("set_pressure_iterations", "iterations"), &FellowGasVolume3D::set_pressure_iterations);
	ClassDB::bind_method(D_METHOD("get_pressure_iterations"), &FellowGasVolume3D::get_pressure_iterations);
	ClassDB::bind_method(D_METHOD("set_pressure_solver", "solver"), &FellowGasVolume3D::set_pressure_solver);
	ClassDB::bind_method(D_METHOD("get_pressure_solver"), &FellowGasVolume3D::get_pressure_solver);
	ClassDB::bind_method(D_METHOD("set_multigrid_cycles", "cycles"), &FellowGasVolume3D::set_multigrid_cycles);
	ClassDB::bind_method(D_METHOD("get_multigrid_cycles"), &FellowGasVolume3D::get_multigrid_cycles);
	ClassDB::bind_method(D_METHOD("set_sparse", "sparse"), &FellowGasVolume3D::set_sparse);
	ClassDB::bind_method(D_METHOD("is_sparse"), &FellowGasVolume3D::is_sparse);
	ClassDB::bind_method(D_METHOD("set_sparse_margin", "bricks"), &FellowGasVolume3D::set_sparse_margin);
	ClassDB::bind_method(D_METHOD("get_sparse_margin"), &FellowGasVolume3D::get_sparse_margin);
	ClassDB::bind_method(D_METHOD("set_maccormack", "enable"), &FellowGasVolume3D::set_maccormack);
	ClassDB::bind_method(D_METHOD("is_maccormack"), &FellowGasVolume3D::is_maccormack);
	ClassDB::bind_method(D_METHOD("set_physics_obstacles", "enable"), &FellowGasVolume3D::set_physics_obstacles);
	ClassDB::bind_method(D_METHOD("is_physics_obstacles"), &FellowGasVolume3D::is_physics_obstacles);
	ClassDB::bind_method(D_METHOD("set_collision_mask", "mask"), &FellowGasVolume3D::set_collision_mask);
	ClassDB::bind_method(D_METHOD("get_collision_mask"), &FellowGasVolume3D::get_collision_mask);
	ClassDB::bind_method(D_METHOD("set_render_enabled", "enable"), &FellowGasVolume3D::set_render_enabled);
	ClassDB::bind_method(D_METHOD("is_render_enabled"), &FellowGasVolume3D::is_render_enabled);
	ClassDB::bind_method(D_METHOD("set_smoke_color", "color"), &FellowGasVolume3D::set_smoke_color);
	ClassDB::bind_method(D_METHOD("get_smoke_color"), &FellowGasVolume3D::get_smoke_color);
	ClassDB::bind_method(D_METHOD("set_smoke_density", "density"), &FellowGasVolume3D::set_smoke_density);
	ClassDB::bind_method(D_METHOD("get_smoke_density"), &FellowGasVolume3D::get_smoke_density);
	ClassDB::bind_method(D_METHOD("set_anisotropy", "g"), &FellowGasVolume3D::set_anisotropy);
	ClassDB::bind_method(D_METHOD("get_anisotropy"), &FellowGasVolume3D::get_anisotropy);
	ClassDB::bind_method(D_METHOD("set_fire_intensity", "intensity"), &FellowGasVolume3D::set_fire_intensity);
	ClassDB::bind_method(D_METHOD("get_fire_intensity"), &FellowGasVolume3D::get_fire_intensity);
	ClassDB::bind_method(D_METHOD("set_fire_temperature_scale", "scale"), &FellowGasVolume3D::set_fire_temperature_scale);
	ClassDB::bind_method(D_METHOD("get_fire_temperature_scale"), &FellowGasVolume3D::get_fire_temperature_scale);
	ClassDB::bind_method(D_METHOD("set_ambient_strength", "strength"), &FellowGasVolume3D::set_ambient_strength);
	ClassDB::bind_method(D_METHOD("get_ambient_strength"), &FellowGasVolume3D::get_ambient_strength);
	ClassDB::bind_method(D_METHOD("set_self_shadow", "strength"), &FellowGasVolume3D::set_self_shadow);
	ClassDB::bind_method(D_METHOD("get_self_shadow"), &FellowGasVolume3D::get_self_shadow);
	ClassDB::bind_method(D_METHOD("set_cast_shadows", "enable"), &FellowGasVolume3D::set_cast_shadows);
	ClassDB::bind_method(D_METHOD("is_casting_shadows"), &FellowGasVolume3D::is_casting_shadows);
	ClassDB::bind_method(D_METHOD("set_shadow_strength", "strength"), &FellowGasVolume3D::set_shadow_strength);
	ClassDB::bind_method(D_METHOD("get_shadow_strength"), &FellowGasVolume3D::get_shadow_strength);
	ClassDB::bind_method(D_METHOD("set_render_step", "cells"), &FellowGasVolume3D::set_render_step);
	ClassDB::bind_method(D_METHOD("get_render_step"), &FellowGasVolume3D::get_render_step);
	ClassDB::bind_method(D_METHOD("set_fire_light", "enable"), &FellowGasVolume3D::set_fire_light);
	ClassDB::bind_method(D_METHOD("is_fire_light"), &FellowGasVolume3D::is_fire_light);
	ClassDB::bind_method(D_METHOD("set_fire_light_energy", "energy"), &FellowGasVolume3D::set_fire_light_energy);
	ClassDB::bind_method(D_METHOD("get_fire_light_energy"), &FellowGasVolume3D::get_fire_light_energy);
	ClassDB::bind_method(D_METHOD("set_fire_light_range", "range"), &FellowGasVolume3D::set_fire_light_range);
	ClassDB::bind_method(D_METHOD("get_fire_light_range"), &FellowGasVolume3D::get_fire_light_range);
	ClassDB::bind_method(D_METHOD("set_fire_light_shadows", "enable"), &FellowGasVolume3D::set_fire_light_shadows);
	ClassDB::bind_method(D_METHOD("is_fire_light_shadows"), &FellowGasVolume3D::is_fire_light_shadows);

	ClassDB::bind_method(D_METHOD("set_velocity_texture_enabled", "enable"), &FellowGasVolume3D::set_velocity_texture_enabled);
	ClassDB::bind_method(D_METHOD("is_velocity_texture_enabled"), &FellowGasVolume3D::is_velocity_texture_enabled);
	ClassDB::bind_method(D_METHOD("set_velocity_texture_scale", "speed"), &FellowGasVolume3D::set_velocity_texture_scale);
	ClassDB::bind_method(D_METHOD("get_velocity_texture_scale"), &FellowGasVolume3D::get_velocity_texture_scale);
	ClassDB::bind_method(D_METHOD("get_velocity_texture"), &FellowGasVolume3D::get_velocity_texture);
	ClassDB::bind_method(D_METHOD("reset_simulation"), &FellowGasVolume3D::reset_simulation);
	ClassDB::bind_method(D_METHOD("get_grid_size"), &FellowGasVolume3D::get_grid_size);
	ClassDB::bind_method(D_METHOD("get_cell_size"), &FellowGasVolume3D::get_cell_size);
	ClassDB::bind_method(D_METHOD("get_vram_bytes"), &FellowGasVolume3D::get_vram_bytes);
	ClassDB::bind_method(D_METHOD("get_steps_simulated"), &FellowGasVolume3D::get_steps_simulated);
	ClassDB::bind_method(D_METHOD("get_fire_power"), &FellowGasVolume3D::get_fire_power);
	ClassDB::bind_method(D_METHOD("get_fire_light_node"), &FellowGasVolume3D::get_fire_light_node);
	ClassDB::bind_method(D_METHOD("request_debug_data", "what"), &FellowGasVolume3D::request_debug_data);
	ClassDB::bind_method(D_METHOD("is_debug_data_ready"), &FellowGasVolume3D::is_debug_data_ready);
	ClassDB::bind_method(D_METHOD("get_debug_data"), &FellowGasVolume3D::get_debug_data);

	ADD_GROUP("Grid", "");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "size", PROPERTY_HINT_NONE, "suffix:m"), "set_size", "get_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "resolution", PROPERTY_HINT_RANGE, "8,512,1"), "set_resolution", "get_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "high_precision"), "set_high_precision", "is_high_precision");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "space", PROPERTY_HINT_ENUM, "Local,World"), "set_space", "get_space");
	ADD_GROUP("Simulation", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "simulation_enabled"), "set_simulation_enabled", "is_simulation_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "substeps", PROPERTY_HINT_RANGE, "1,8,1"), "set_substeps", "get_substeps");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_scale", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_time_scale", "get_time_scale");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "prewarm_steps", PROPERTY_HINT_RANGE, "0,10000,1"), "set_prewarm_steps", "get_prewarm_steps");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "closed_faces", PROPERTY_HINT_FLAGS, "-X,+X,-Y (Floor),+Y,-Z,+Z"), "set_closed_faces", "get_closed_faces");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "buoyancy", PROPERTY_HINT_RANGE, "-2,4,0.01"), "set_buoyancy", "get_buoyancy");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoke_weight", PROPERTY_HINT_RANGE, "-10,10,0.01,suffix:m/s²"), "set_smoke_weight", "get_smoke_weight");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "vorticity", PROPERTY_HINT_RANGE, "0,2,0.01"), "set_vorticity", "get_vorticity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "turbulence", PROPERTY_HINT_RANGE, "0,50,0.01,suffix:m/s²"), "set_turbulence", "get_turbulence");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "turbulence_scale", PROPERTY_HINT_RANGE, "0.01,5,0.01,suffix:m"), "set_turbulence_scale", "get_turbulence_scale");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR3, "wind", PROPERTY_HINT_NONE, "suffix:m/s"), "set_wind", "get_wind");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "wind_rate", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:/s"), "set_wind_rate", "get_wind_rate");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "wind_from_scene"), "set_wind_from_scene", "is_wind_from_scene");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoke_dissipation", PROPERTY_HINT_RANGE, "0,5,0.001,suffix:/s"), "set_smoke_dissipation", "get_smoke_dissipation");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "cooling", PROPERTY_HINT_RANGE, "0,10,0.01,suffix:/s"), "set_cooling", "get_cooling");
	ADD_SUBGROUP("Combustion", "");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ignition_temperature", PROPERTY_HINT_RANGE, "0,2000,1,suffix:K"), "set_ignition_temperature", "get_ignition_temperature");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "flame_temperature_limit", PROPERTY_HINT_RANGE, "0,4000,1,suffix:K"), "set_flame_temperature_limit", "get_flame_temperature_limit");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "burn_rate", PROPERTY_HINT_RANGE, "0,50,0.01,suffix:/s"), "set_burn_rate", "get_burn_rate");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "heat_release", PROPERTY_HINT_RANGE, "0,5000,1,suffix:K"), "set_heat_release", "get_heat_release");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "soot", PROPERTY_HINT_RANGE, "0,5,0.01"), "set_soot", "get_soot");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "expansion", PROPERTY_HINT_RANGE, "0,5,0.01"), "set_expansion", "get_expansion");
	ADD_SUBGROUP("Solver", "");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pressure_solver", PROPERTY_HINT_ENUM, "SOR,Multigrid"), "set_pressure_solver", "get_pressure_solver");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "multigrid_cycles", PROPERTY_HINT_RANGE, "1,8,1"), "set_multigrid_cycles", "get_multigrid_cycles");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "sparse"), "set_sparse", "is_sparse");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "sparse_margin", PROPERTY_HINT_RANGE, "1,4,1"), "set_sparse_margin", "get_sparse_margin");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "pressure_iterations", PROPERTY_HINT_RANGE, "0,200,1"), "set_pressure_iterations", "get_pressure_iterations");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "maccormack"), "set_maccormack", "is_maccormack");
	ADD_SUBGROUP("Obstacles", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "physics_obstacles"), "set_physics_obstacles", "is_physics_obstacles");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "collision_mask", PROPERTY_HINT_LAYERS_3D_PHYSICS), "set_collision_mask", "get_collision_mask");
	ADD_GROUP("Rendering", "");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "render_enabled"), "set_render_enabled", "is_render_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::COLOR, "smoke_color", PROPERTY_HINT_COLOR_NO_ALPHA), "set_smoke_color", "get_smoke_color");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "smoke_density", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:/m"), "set_smoke_density", "get_smoke_density");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "anisotropy", PROPERTY_HINT_RANGE, "-0.95,0.95,0.01"), "set_anisotropy", "get_anisotropy");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fire_intensity", PROPERTY_HINT_RANGE, "0,500,0.01,or_greater"), "set_fire_intensity", "get_fire_intensity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fire_temperature_scale", PROPERTY_HINT_RANGE, "0.25,4,0.01"), "set_fire_temperature_scale", "get_fire_temperature_scale");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "ambient_strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_ambient_strength", "get_ambient_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "self_shadow", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_self_shadow", "get_self_shadow");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "cast_shadows"), "set_cast_shadows", "is_casting_shadows");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "shadow_strength", PROPERTY_HINT_RANGE, "0,4,0.01"), "set_shadow_strength", "get_shadow_strength");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "render_step", PROPERTY_HINT_RANGE, "0.25,4,0.01,suffix:cells"), "set_render_step", "get_render_step");
	ADD_GROUP("Fire Light", "fire_light_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fire_light"), "set_fire_light", "is_fire_light");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fire_light_energy", PROPERTY_HINT_RANGE, "0,16,0.01,or_greater"), "set_fire_light_energy", "get_fire_light_energy");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "fire_light_range", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater,suffix:m"), "set_fire_light_range", "get_fire_light_range");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "fire_light_shadows"), "set_fire_light_shadows", "is_fire_light_shadows");

	ADD_GROUP("Velocity Texture", "velocity_texture_");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "velocity_texture_enabled"), "set_velocity_texture_enabled", "is_velocity_texture_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "velocity_texture_scale", PROPERTY_HINT_RANGE, "0.1,50,0.1,suffix:m/s"), "set_velocity_texture_scale", "get_velocity_texture_scale");

	BIND_ENUM_CONSTANT(SPACE_LOCAL);
	BIND_ENUM_CONSTANT(SPACE_WORLD);
	BIND_ENUM_CONSTANT(SOLVER_SOR);
	BIND_ENUM_CONSTANT(SOLVER_MULTIGRID);
	BIND_ENUM_CONSTANT(FACE_NEG_X);
	BIND_ENUM_CONSTANT(FACE_POS_X);
	BIND_ENUM_CONSTANT(FACE_NEG_Y);
	BIND_ENUM_CONSTANT(FACE_POS_Y);
	BIND_ENUM_CONSTANT(FACE_NEG_Z);
	BIND_ENUM_CONSTANT(FACE_POS_Z);
}
