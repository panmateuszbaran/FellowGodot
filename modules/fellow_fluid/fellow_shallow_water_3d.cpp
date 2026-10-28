/**************************************************************************/
/*  fellow_shallow_water_3d.cpp                                           */
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

#include "fellow_shallow_water_3d.h"

#include "fellow_swe.glsl.gen.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "servers/rendering/fellow_settings.h"
#include "servers/rendering/rendering_server.h"

// Water surface shader (Godot shading language). Displacement from the simulated surface height, normals from
// the simulated gradient, foam from the activity channel, refraction through the screen texture with
// Beer-Lambert absorption over the refracted water thickness, reflections from the engine's specular IBL
// (sky / reflection probes) with Schlick Fresnel on the refracted part.
static const char *fellow_water_shader_code = R"(
shader_type spatial;
render_mode blend_mix, cull_disabled, depth_draw_always, diffuse_lambert, specular_schlick_ggx;

uniform sampler2D surface_tex : filter_linear, repeat_disable;
uniform sampler2D terrain_tex : filter_linear, repeat_disable;
uniform sampler2D screen_tex : hint_screen_texture, filter_linear_mipmap, repeat_disable;
uniform sampler2D depth_tex : hint_depth_texture, filter_nearest, repeat_disable;
uniform vec3 absorption = vec3(0.45, 0.10, 0.07);
uniform vec3 scatter_color : source_color = vec3(0.02, 0.10, 0.11);
uniform float refraction_strength = 0.05;
uniform float foam_albedo = 0.85;

varying vec2 v_uv;

void vertex() {
	v_uv = UV;
	VERTEX.y = textureLod(surface_tex, UV, 0.0).r;
}

void fragment() {
	vec4 s = texture(surface_tex, v_uv);
	float water_depth = s.r - texture(terrain_tex, v_uv).r;
	if (water_depth < 0.003) {
		discard;
	}
	vec3 n_world = normalize((MODEL_MATRIX * vec4(normalize(vec3(-s.g, 1.0, -s.b)), 0.0)).xyz);
	NORMAL = normalize((VIEW_MATRIX * vec4(n_world, 0.0)).xyz);

	vec2 ruv = clamp(SCREEN_UV + n_world.xz * refraction_strength, vec2(0.001), vec2(0.999));
	float d = textureLod(depth_tex, ruv, 0.0).r;
	vec4 vp = INV_PROJECTION_MATRIX * vec4(ruv * 2.0 - 1.0, d, 1.0);
	vp.xyz /= vp.w;
	if (-vp.z < -VERTEX.z) {
		// Refracted sample lies in front of the surface: use the undistorted position.
		ruv = SCREEN_UV;
		d = textureLod(depth_tex, ruv, 0.0).r;
		vp = INV_PROJECTION_MATRIX * vec4(ruv * 2.0 - 1.0, d, 1.0);
		vp.xyz /= vp.w;
	}
	float thickness = max(VERTEX.z - vp.z, 0.0);
	vec3 transmittance = exp(-absorption * thickness);
	vec3 refracted = textureLod(screen_tex, ruv, 0.0).rgb * transmittance + scatter_color * (1.0 - transmittance);

	float foam = clamp(s.a, 0.0, 1.0);
	float nv = clamp(dot(NORMAL, VIEW), 0.0, 1.0);
	float fresnel = 0.02 + 0.98 * pow(1.0 - nv, 5.0);
	ALBEDO = vec3(foam_albedo * foam);
	EMISSION = refracted * (1.0 - fresnel) * (1.0 - foam);
	ROUGHNESS = mix(0.03, 0.7, foam);
	METALLIC = 0.0;
	SPECULAR = 0.25;
	ALPHA = smoothstep(0.003, 0.04, water_depth);
}
)";

static const char *kernel_names[] = { "advect", "height", "velocity", "surface" };

FellowShallowWater3D::FellowShallowWater3D() {
	surface_texture.instantiate();
	terrain_texture.instantiate();
	flow_texture.instantiate();
}

FellowShallowWater3D::~FellowShallowWater3D() {
	if (RS::get_singleton() && rd) {
		if (gpu_ready) {
			_rt_free();
		}
		for (int k = 0; k < KERNEL_MAX; k++) {
			// Freeing a shader also frees the pipelines created from it.
			if (shaders[k].is_valid()) {
				rd->free_rid(shaders[k]);
				shaders[k] = RID();
				pipelines[k] = RID();
			}
		}
	}
}

Vector2 FellowShallowWater3D::_world_to_cells(const Vector3 &p_world, float *r_local_y) const {
	Vector3 local = is_inside_tree() ? get_global_transform().affine_inverse().xform(p_world) : p_world;
	if (r_local_y) {
		*r_local_y = local.y;
	}
	return Vector2((local.x / domain_size.x + 0.5f) * resolution, (local.z / domain_size.y + 0.5f) * resolution);
}

// ------------------------------------------------------------------ settings

void FellowShallowWater3D::set_resolution(int p_resolution) {
	ERR_FAIL_COND_MSG(p_resolution < 16 || p_resolution > 4096, "Resolution must be in [16, 4096].");
	resolution = p_resolution;
	needs_reset = true;
	if (is_inside_tree()) {
		_update_mesh();
	}
}

void FellowShallowWater3D::set_domain_size(const Vector2 &p_size) {
	ERR_FAIL_COND(p_size.x <= 0.0 || p_size.y <= 0.0);
	domain_size = p_size;
	needs_reset = true;
	if (is_inside_tree()) {
		_update_mesh();
	}
}

void FellowShallowWater3D::set_substeps(int p_substeps) {
	substeps = CLAMP(p_substeps, 0, 32);
}

void FellowShallowWater3D::set_time_step(float p_dt) {
	ERR_FAIL_COND(p_dt <= 0.0f);
	time_step = p_dt;
}

void FellowShallowWater3D::set_initial_water_level(float p_level) {
	initial_water_level = p_level;
	needs_reset = true;
}

void FellowShallowWater3D::set_render_grid(int p_grid) {
	render_grid = CLAMP(p_grid, 8, 2048);
	if (is_inside_tree()) {
		_update_mesh();
	}
}

void FellowShallowWater3D::set_terrain_image(const Ref<Image> &p_image) {
	terrain_image = p_image;
	needs_reset = true;
}

void FellowShallowWater3D::set_collider(int p_index, const Vector3 &p_world_position, float p_radius, const Vector3 &p_world_velocity) {
	ERR_FAIL_INDEX(p_index, MAX_COLLIDERS);
	if (colliders_pos_radius.size() <= p_index) {
		colliders_pos_radius.resize(p_index + 1);
		colliders_velocity.resize(p_index + 1);
	}
	float ly = 0.0f;
	Vector2 cell = _world_to_cells(p_world_position, &ly);
	Vector3 lv = is_inside_tree() ? get_global_transform().basis.inverse().xform(p_world_velocity) : p_world_velocity;
	colliders_pos_radius.write[p_index] = Vector4(cell.x, cell.y, ly, p_radius);
	colliders_velocity.write[p_index] = Vector4(lv.x, lv.y, lv.z, 0.0f);
}

void FellowShallowWater3D::clear_colliders() {
	colliders_pos_radius.clear();
	colliders_velocity.clear();
}

void FellowShallowWater3D::add_impulse(const Vector3 &p_world_position, float p_radius, float p_depth_change) {
	if (pending_impulses.size() >= MAX_IMPULSES) {
		WARN_PRINT_ONCE("FellowShallowWater3D: more than 16 impulses in one frame, extra impulses dropped.");
		return;
	}
	Vector2 cell = _world_to_cells(p_world_position);
	pending_impulses.push_back(Vector4(cell.x, cell.y, p_radius / _dx(), p_depth_change));
}

void FellowShallowWater3D::reset_simulation() {
	needs_reset = true;
}

int64_t FellowShallowWater3D::get_simulation_vram_bytes() const {
	int64_t n = int64_t(resolution) * resolution;
	return n * 16 * 3 + n * 4 + n * 8 * 2 + int64_t(sizeof(InteractionBuffer)); // state x3, terrain, surface, flow
}

// ------------------------------------------------------------------ CPU-side setup

Ref<Image> FellowShallowWater3D::_make_terrain(int p_res) const {
	Ref<Image> img;
	if (terrain_image.is_valid() && !terrain_image->is_empty()) {
		img = terrain_image->duplicate();
		if (img->is_compressed()) {
			img->decompress();
		}
		img->convert(Image::FORMAT_RF);
		if (img->get_width() != p_res || img->get_height() != p_res) {
			// Nearest: bilinear resampling would create intermediate bed heights around solid obstacles
			// (thin dry "walls" that stretch the water surface). Provide the image at the grid resolution
			// for smooth terrain.
			img->resize(p_res, p_res, Image::INTERPOLATE_NEAREST);
		}
	} else {
		img = Image::create_empty(p_res, p_res, false, Image::FORMAT_RF);
		img->fill(Color(0, 0, 0));
	}
	return img;
}

void FellowShallowWater3D::_update_mesh() {
	int n = MIN(resolution, render_grid);
	if (mesh_instance && n == mesh_grid) {
		mesh_instance->set_custom_aabb(AABB(Vector3(-domain_size.x * 0.5f, -64.0f, -domain_size.y * 0.5f), Vector3(domain_size.x, 128.0f, domain_size.y)));
		return;
	}
	PackedVector3Array verts;
	PackedVector3Array normals;
	PackedVector2Array uvs;
	PackedInt32Array indices;
	verts.resize((n + 1) * (n + 1));
	normals.resize((n + 1) * (n + 1));
	uvs.resize((n + 1) * (n + 1));
	indices.resize(n * n * 6);
	Vector3 *vw = verts.ptrw();
	Vector3 *nw = normals.ptrw();
	Vector2 *uw = uvs.ptrw();
	for (int j = 0; j <= n; j++) {
		for (int i = 0; i <= n; i++) {
			float u = float(i) / n;
			float v = float(j) / n;
			int k = j * (n + 1) + i;
			vw[k] = Vector3((u - 0.5f) * domain_size.x, 0.0f, (v - 0.5f) * domain_size.y);
			nw[k] = Vector3(0, 1, 0);
			uw[k] = Vector2(u, v);
		}
	}
	int32_t *iw = indices.ptrw();
	int w = 0;
	for (int j = 0; j < n; j++) {
		for (int i = 0; i < n; i++) {
			int a = j * (n + 1) + i;
			int b = a + 1;
			int c = a + (n + 1);
			int d = c + 1;
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
	if (!mesh_instance) {
		mesh_instance = memnew(MeshInstance3D);
		mesh_instance->set_name("FellowWaterSurface");
		mesh_instance->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
		add_child(mesh_instance, false, INTERNAL_MODE_FRONT);
	}
	mesh_instance->set_mesh(mesh);
	mesh_instance->set_custom_aabb(AABB(Vector3(-domain_size.x * 0.5f, -64.0f, -domain_size.y * 0.5f), Vector3(domain_size.x, 128.0f, domain_size.y)));
	mesh_grid = n;
	_update_material();
}

void FellowShallowWater3D::_update_material() {
	if (water_shader.is_null()) {
		water_shader.instantiate();
		water_shader->set_code(fellow_water_shader_code);
	}
	if (material.is_null()) {
		material.instantiate();
		material->set_shader(water_shader);
	}
	material->set_shader_parameter("surface_tex", surface_texture);
	material->set_shader_parameter("terrain_tex", terrain_texture);
	if (mesh_instance) {
		mesh_instance->set_material_override(material);
	}
}

// ------------------------------------------------------------------ render thread

RID FellowShallowWater3D::_get_uniform_set(Kernel p_kernel, RID p_in, RID p_out) {
	Vector<RD::Uniform> u;
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 0, p_in));
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 1, terrain_tex));
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 2, p_out));
	u.push_back(RD::Uniform(RD::UNIFORM_TYPE_STORAGE_BUFFER, 3, interaction_buffer));
	if (p_kernel == KERNEL_SURFACE) {
		u.push_back(RD::Uniform(RD::UNIFORM_TYPE_IMAGE, 4, flow_tex));
	}
	return rd->uniform_set_create(u, shaders[p_kernel], 0);
}

void FellowShallowWater3D::_rt_create() {
	rd = RS::get_singleton()->get_rendering_device();
	if (!rd) {
		gpu_failed = true;
		ERR_PRINT("FellowShallowWater3D requires a RenderingDevice renderer (Forward+ or Mobile).");
		return;
	}
	if (shader_file.is_null()) {
		shader_file.instantiate();
		Error err = shader_file->parse_versions_from_text(fellow_swe_shader_glsl);
		if (err != OK) {
			shader_file->print_errors("fellow_swe");
			gpu_failed = true;
			return;
		}
	}
	for (int k = 0; k < KERNEL_MAX; k++) {
		if (shaders[k].is_null()) {
			shaders[k] = rd->shader_create_from_spirv(shader_file->get_spirv_stages(kernel_names[k]), String("FellowSWE ") + kernel_names[k]);
			ERR_FAIL_COND_MSG(shaders[k].is_null(), "FellowShallowWater3D: shader creation failed.");
			pipelines[k] = rd->compute_pipeline_create(shaders[k]);
		}
	}

	RD::TextureFormat tf;
	tf.width = resolution;
	tf.height = resolution;
	tf.format = RD::DATA_FORMAT_R32G32B32A32_SFLOAT;
	tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT | RD::TEXTURE_USAGE_CAN_UPDATE_BIT;
	for (int i = 0; i < 3; i++) {
		state[i] = rd->texture_create(tf, RD::TextureView());
		rd->set_resource_name(state[i], "FellowSWE state " + itos(i));
	}
	tf.format = RD::DATA_FORMAT_R32_SFLOAT;
	terrain_tex = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(terrain_tex, "FellowSWE terrain");
	tf.format = RD::DATA_FORMAT_R16G16B16A16_SFLOAT;
	tf.usage_bits = RD::TEXTURE_USAGE_SAMPLING_BIT | RD::TEXTURE_USAGE_STORAGE_BIT;
	surface_tex = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(surface_tex, "FellowSWE surface");
	flow_tex = rd->texture_create(tf, RD::TextureView());
	rd->set_resource_name(flow_tex, "FellowSWE flow");
	interaction_buffer = rd->storage_buffer_create(sizeof(InteractionBuffer));

	uniform_sets[KERNEL_ADVECT] = _get_uniform_set(KERNEL_ADVECT, state[0], state[1]);
	uniform_sets[KERNEL_HEIGHT] = _get_uniform_set(KERNEL_HEIGHT, state[1], state[2]);
	uniform_sets[KERNEL_VELOCITY] = _get_uniform_set(KERNEL_VELOCITY, state[2], state[0]);
	uniform_sets[KERNEL_SURFACE] = _get_uniform_set(KERNEL_SURFACE, state[0], surface_tex);

	surface_texture->set_texture_rd_rid(surface_tex);
	terrain_texture->set_texture_rd_rid(terrain_tex);
	flow_texture->set_texture_rd_rid(flow_tex);
	allocated_resolution = resolution;
	gpu_ready = true;
}

void FellowShallowWater3D::_rt_free() {
	if (!rd) {
		return;
	}
	surface_texture->set_texture_rd_rid(RID());
	terrain_texture->set_texture_rd_rid(RID());
	flow_texture->set_texture_rd_rid(RID());
	for (int k = 0; k < KERNEL_MAX; k++) {
		if (uniform_sets[k].is_valid() && rd->uniform_set_is_valid(uniform_sets[k])) {
			rd->free_rid(uniform_sets[k]);
		}
		uniform_sets[k] = RID();
	}
	for (int i = 0; i < 3; i++) {
		if (state[i].is_valid()) {
			rd->free_rid(state[i]);
			state[i] = RID();
		}
	}
	RID *others[] = { &terrain_tex, &surface_tex, &flow_tex, &interaction_buffer };
	for (RID *r : others) {
		if (r->is_valid()) {
			rd->free_rid(*r);
			*r = RID();
		}
	}
	gpu_ready = false;
	allocated_resolution = 0;
}

void FellowShallowWater3D::_dispatch(RD::ComputeListID p_list, Kernel p_kernel, const PushConstant &p_pc) {
	rd->compute_list_bind_compute_pipeline(p_list, pipelines[p_kernel]);
	rd->compute_list_bind_uniform_set(p_list, uniform_sets[p_kernel], 0);
	rd->compute_list_set_push_constant(p_list, &p_pc, sizeof(PushConstant));
	rd->compute_list_dispatch(p_list, (resolution + 7) / 8, (resolution + 7) / 8, 1);
}

void FellowShallowWater3D::_rt_read_timestamps() {
	uint32_t n = rd->get_captured_timestamps_count();
	uint64_t t0 = 0, t1 = 0, t2 = 0;
	int found = 0;
	for (uint32_t i = 0; i < n; i++) {
		String name = rd->get_captured_timestamp_name(i);
		if (!name.begins_with("FellowFluid:")) {
			continue;
		}
		if (name == "FellowFluid:SimBegin") {
			t0 = rd->get_captured_timestamp_gpu_time(i);
			found |= 1;
		} else if (name == "FellowFluid:SurfaceBegin") {
			t1 = rd->get_captured_timestamp_gpu_time(i);
			found |= 2;
		} else if (name == "FellowFluid:SurfaceEnd") {
			t2 = rd->get_captured_timestamp_gpu_time(i);
			found |= 4;
		}
	}
	if (found == 7) {
		sim_gpu_ms = double(t1 - t0) / 1e6;
		surface_gpu_ms = double(t2 - t1) / 1e6;
	}
}

void FellowShallowWater3D::_rt_reset() {
	Ref<Image> t = _make_terrain(resolution);
	Vector<uint8_t> tdata = t->get_data();
	rd->texture_update(terrain_tex, 0, tdata);
	const float *bed = reinterpret_cast<const float *>(tdata.ptr());
	Vector<uint8_t> bytes;
	bytes.resize(int64_t(resolution) * resolution * 16);
	float *s = reinterpret_cast<float *>(bytes.ptrw());
	for (int64_t i = 0; i < int64_t(resolution) * resolution; i++) {
		float b = bed[i];
		float h = (b >= solid_height) ? 0.0f : MAX(initial_water_level - b, 0.0f);
		s[i * 4 + 0] = h;
		s[i * 4 + 1] = 0.0f;
		s[i * 4 + 2] = 0.0f;
		s[i * 4 + 3] = 0.0f;
	}
	for (int i = 0; i < 3; i++) {
		rd->texture_update(state[i], 0, bytes);
	}
	steps_simulated = 0;
}

void FellowShallowWater3D::_rt_step(int p_steps, PackedByteArray p_interaction, int p_colliders, int p_impulses) {
	if (gpu_failed) {
		return;
	}
	if (gpu_ready && allocated_resolution != resolution) {
		_rt_free();
		needs_reset = true;
	}
	if (!gpu_ready) {
		_rt_create();
		if (!gpu_ready) {
			return;
		}
		needs_reset = true;
	}
	if (needs_reset) {
		_rt_reset();
		needs_reset = false;
	}
	_rt_read_timestamps();
	if (p_steps <= 0) {
		return;
	}
	rd->buffer_update(interaction_buffer, 0, p_interaction.size(), p_interaction.ptr());

	PushConstant pc;
	memset(&pc, 0, sizeof(PushConstant));
	pc.size[0] = resolution;
	pc.size[1] = resolution;
	pc.dx = _dx();
	pc.dt = time_step;
	pc.gravity = gravity;
	pc.damping = damping;
	pc.solid_height = solid_height;
	pc.foam_decay = foam_decay;
	pc.inflow_velocity[0] = inflow_velocity.x;
	pc.inflow_velocity[1] = inflow_velocity.y;
	pc.inflow_depth = inflow_depth;
	pc.inflow_cells = inflow_cells;
	pc.collider_count = p_colliders;
	pc.max_speed = pc.dx / pc.dt; // semi-Lagrangian advection + outflow limiter keep this stable
	pc.collider_drag = collider_drag;

	rd->draw_command_begin_label("FellowEngine shallow water");
	rd->capture_timestamp("FellowFluid:SimBegin");
	for (int s = 0; s < p_steps; s++) {
		pc.impulse_count = s == 0 ? p_impulses : 0;
		for (int k = KERNEL_ADVECT; k <= KERNEL_VELOCITY; k++) {
			RD::ComputeListID list = rd->compute_list_begin();
			_dispatch(list, Kernel(k), pc);
			rd->compute_list_end();
		}
	}
	rd->capture_timestamp("FellowFluid:SurfaceBegin");
	{
		RD::ComputeListID list = rd->compute_list_begin();
		_dispatch(list, KERNEL_SURFACE, pc);
		rd->compute_list_end();
	}
	rd->capture_timestamp("FellowFluid:SurfaceEnd");
	rd->draw_command_end_label();
	steps_simulated += p_steps;
	frames_dispatched++;
}

// ------------------------------------------------------------------ main thread

void FellowShallowWater3D::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			_update_mesh();
			set_process_internal(true);
		} break;
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (!Fellow::fluid_enabled() || gpu_failed) {
				return;
			}
			InteractionBuffer ib;
			memset(&ib, 0, sizeof(InteractionBuffer));
			int nc = MIN(colliders_pos_radius.size(), MAX_COLLIDERS);
			for (int i = 0; i < nc; i++) {
				const Vector4 &p = colliders_pos_radius[i];
				const Vector4 &v = colliders_velocity[i];
				ib.collider_pos_radius[i][0] = p.x;
				ib.collider_pos_radius[i][1] = p.y;
				ib.collider_pos_radius[i][2] = p.z;
				ib.collider_pos_radius[i][3] = p.w;
				ib.collider_vel[i][0] = v.x;
				ib.collider_vel[i][1] = v.y;
				ib.collider_vel[i][2] = v.z;
			}
			int ni = MIN(pending_impulses.size(), MAX_IMPULSES);
			for (int i = 0; i < ni; i++) {
				const Vector4 &p = pending_impulses[i];
				ib.impulse[i][0] = p.x;
				ib.impulse[i][1] = p.y;
				ib.impulse[i][2] = p.z;
				ib.impulse[i][3] = p.w;
			}
			PackedByteArray bytes;
			bytes.resize(sizeof(InteractionBuffer));
			memcpy(bytes.ptrw(), &ib, sizeof(InteractionBuffer));
			pending_impulses.clear();
			int steps = simulation_enabled ? substeps : 0;
			RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowShallowWater3D::_rt_step).bind(steps, bytes, nc, ni));
		} break;
		case NOTIFICATION_EXIT_TREE: {
			if (gpu_ready) {
				RS::get_singleton()->call_on_render_thread(callable_mp(this, &FellowShallowWater3D::_rt_free));
			}
		} break;
	}
}

void FellowShallowWater3D::_bind_methods() {
	ClassDB::bind_method(D_METHOD("set_resolution", "resolution"), &FellowShallowWater3D::set_resolution);
	ClassDB::bind_method(D_METHOD("get_resolution"), &FellowShallowWater3D::get_resolution);
	ClassDB::bind_method(D_METHOD("set_domain_size", "size"), &FellowShallowWater3D::set_domain_size);
	ClassDB::bind_method(D_METHOD("get_domain_size"), &FellowShallowWater3D::get_domain_size);
	ClassDB::bind_method(D_METHOD("set_substeps", "substeps"), &FellowShallowWater3D::set_substeps);
	ClassDB::bind_method(D_METHOD("get_substeps"), &FellowShallowWater3D::get_substeps);
	ClassDB::bind_method(D_METHOD("set_time_step", "seconds"), &FellowShallowWater3D::set_time_step);
	ClassDB::bind_method(D_METHOD("get_time_step"), &FellowShallowWater3D::get_time_step);
	ClassDB::bind_method(D_METHOD("set_gravity", "gravity"), &FellowShallowWater3D::set_gravity);
	ClassDB::bind_method(D_METHOD("get_gravity"), &FellowShallowWater3D::get_gravity);
	ClassDB::bind_method(D_METHOD("set_damping", "damping"), &FellowShallowWater3D::set_damping);
	ClassDB::bind_method(D_METHOD("get_damping"), &FellowShallowWater3D::get_damping);
	ClassDB::bind_method(D_METHOD("set_foam_decay", "decay"), &FellowShallowWater3D::set_foam_decay);
	ClassDB::bind_method(D_METHOD("get_foam_decay"), &FellowShallowWater3D::get_foam_decay);
	ClassDB::bind_method(D_METHOD("set_solid_height", "height"), &FellowShallowWater3D::set_solid_height);
	ClassDB::bind_method(D_METHOD("get_solid_height"), &FellowShallowWater3D::get_solid_height);
	ClassDB::bind_method(D_METHOD("set_collider_drag", "drag"), &FellowShallowWater3D::set_collider_drag);
	ClassDB::bind_method(D_METHOD("get_collider_drag"), &FellowShallowWater3D::get_collider_drag);
	ClassDB::bind_method(D_METHOD("set_inflow_velocity", "velocity"), &FellowShallowWater3D::set_inflow_velocity);
	ClassDB::bind_method(D_METHOD("get_inflow_velocity"), &FellowShallowWater3D::get_inflow_velocity);
	ClassDB::bind_method(D_METHOD("set_inflow_depth", "depth"), &FellowShallowWater3D::set_inflow_depth);
	ClassDB::bind_method(D_METHOD("get_inflow_depth"), &FellowShallowWater3D::get_inflow_depth);
	ClassDB::bind_method(D_METHOD("set_inflow_cells", "cells"), &FellowShallowWater3D::set_inflow_cells);
	ClassDB::bind_method(D_METHOD("get_inflow_cells"), &FellowShallowWater3D::get_inflow_cells);
	ClassDB::bind_method(D_METHOD("set_initial_water_level", "level"), &FellowShallowWater3D::set_initial_water_level);
	ClassDB::bind_method(D_METHOD("get_initial_water_level"), &FellowShallowWater3D::get_initial_water_level);
	ClassDB::bind_method(D_METHOD("set_render_grid", "grid"), &FellowShallowWater3D::set_render_grid);
	ClassDB::bind_method(D_METHOD("get_render_grid"), &FellowShallowWater3D::get_render_grid);
	ClassDB::bind_method(D_METHOD("set_simulation_enabled", "enabled"), &FellowShallowWater3D::set_simulation_enabled);
	ClassDB::bind_method(D_METHOD("is_simulation_enabled"), &FellowShallowWater3D::is_simulation_enabled);
	ClassDB::bind_method(D_METHOD("set_terrain_image", "image"), &FellowShallowWater3D::set_terrain_image);
	ClassDB::bind_method(D_METHOD("get_terrain_image"), &FellowShallowWater3D::get_terrain_image);

	ClassDB::bind_method(D_METHOD("set_collider", "index", "world_position", "radius", "world_velocity"), &FellowShallowWater3D::set_collider);
	ClassDB::bind_method(D_METHOD("clear_colliders"), &FellowShallowWater3D::clear_colliders);
	ClassDB::bind_method(D_METHOD("add_impulse", "world_position", "radius", "depth_change"), &FellowShallowWater3D::add_impulse);
	ClassDB::bind_method(D_METHOD("reset_simulation"), &FellowShallowWater3D::reset_simulation);
	ClassDB::bind_method(D_METHOD("get_simulation_gpu_time_ms"), &FellowShallowWater3D::get_simulation_gpu_time_ms);
	ClassDB::bind_method(D_METHOD("get_surface_gpu_time_ms"), &FellowShallowWater3D::get_surface_gpu_time_ms);
	ClassDB::bind_method(D_METHOD("get_simulation_vram_bytes"), &FellowShallowWater3D::get_simulation_vram_bytes);
	ClassDB::bind_method(D_METHOD("get_steps_simulated"), &FellowShallowWater3D::get_steps_simulated);
	ClassDB::bind_method(D_METHOD("get_surface_texture"), &FellowShallowWater3D::get_surface_texture);
	ClassDB::bind_method(D_METHOD("get_terrain_texture"), &FellowShallowWater3D::get_terrain_texture);
	ClassDB::bind_method(D_METHOD("get_flow_texture"), &FellowShallowWater3D::get_flow_texture);
	ClassDB::bind_method(D_METHOD("get_water_mesh_instance"), &FellowShallowWater3D::get_water_mesh_instance);

	ADD_PROPERTY(PropertyInfo(Variant::INT, "resolution", PROPERTY_HINT_RANGE, "16,4096,1"), "set_resolution", "get_resolution");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "domain_size", PROPERTY_HINT_NONE, "suffix:m"), "set_domain_size", "get_domain_size");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "substeps", PROPERTY_HINT_RANGE, "0,32,1"), "set_substeps", "get_substeps");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "time_step", PROPERTY_HINT_RANGE, "0.0005,0.05,0.0001,suffix:s"), "set_time_step", "get_time_step");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "gravity", PROPERTY_HINT_RANGE, "0,30,0.01"), "set_gravity", "get_gravity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "damping", PROPERTY_HINT_RANGE, "0,5,0.001"), "set_damping", "get_damping");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "foam_decay", PROPERTY_HINT_RANGE, "0,10,0.01"), "set_foam_decay", "get_foam_decay");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "solid_height", PROPERTY_HINT_NONE, "suffix:m"), "set_solid_height", "get_solid_height");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "collider_drag", PROPERTY_HINT_RANGE, "0,1,0.01"), "set_collider_drag", "get_collider_drag");
	ADD_PROPERTY(PropertyInfo(Variant::VECTOR2, "inflow_velocity", PROPERTY_HINT_NONE, "suffix:m/s"), "set_inflow_velocity", "get_inflow_velocity");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "inflow_depth", PROPERTY_HINT_NONE, "suffix:m"), "set_inflow_depth", "get_inflow_depth");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "inflow_cells", PROPERTY_HINT_RANGE, "1,64,1"), "set_inflow_cells", "get_inflow_cells");
	ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "initial_water_level", PROPERTY_HINT_NONE, "suffix:m"), "set_initial_water_level", "get_initial_water_level");
	ADD_PROPERTY(PropertyInfo(Variant::INT, "render_grid", PROPERTY_HINT_RANGE, "8,2048,1"), "set_render_grid", "get_render_grid");
	ADD_PROPERTY(PropertyInfo(Variant::BOOL, "simulation_enabled"), "set_simulation_enabled", "is_simulation_enabled");
	ADD_PROPERTY(PropertyInfo(Variant::OBJECT, "terrain_image", PROPERTY_HINT_RESOURCE_TYPE, "Image"), "set_terrain_image", "get_terrain_image");
}
