/**************************************************************************/
/*  fellow_impostor_baker.cpp                                             */
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

#include "fellow_impostor_baker.h"

#include "core/object/callable_mp.h"
#include "core/object/class_db.h"
#include "core/os/os.h"
#include "scene/3d/camera_3d.h"
#include "scene/3d/mesh_instance_3d.h"
#include "scene/main/viewport.h"
#include "scene/resources/3d/world_3d.h"
#include "scene/resources/environment.h"
#include "scene/resources/image_texture.h"
#include "scene/resources/material.h"
#include "scene/resources/shader.h"
#include "servers/rendering/rendering_server.h"

// The octahedral mapping (y up) and each view's camera basis; the impostor shader uses the same.
static Vector3 octa_decode(const Vector2 &p_uv) {
	const Vector2 p = p_uv * 2.0 - Vector2(1.0, 1.0);
	Vector3 d(p.x, 1.0 - Math::abs(p.x) - Math::abs(p.y), p.y);
	if (d.y < 0.0) {
		const real_t sx = d.x >= 0.0 ? 1.0 : -1.0;
		const real_t sz = d.z >= 0.0 ? 1.0 : -1.0;
		const real_t nx = (1.0 - Math::abs(d.z)) * sx;
		const real_t nz = (1.0 - Math::abs(d.x)) * sz;
		d.x = nx;
		d.z = nz;
	}
	return d.normalized();
}

Vector3 FellowImpostorBaker::frame_direction(int p_x, int p_y, int p_frames) {
	return octa_decode(Vector2((p_x + 0.5) / p_frames, (p_y + 0.5) / p_frames));
}

void FellowImpostorBaker::frame_basis(const Vector3 &p_dir, Vector3 &r_right, Vector3 &r_up) {
	const Vector3 ref = Math::abs(p_dir.y) > 0.999 ? Vector3(0, 0, 1) : Vector3(0, 1, 0);
	r_right = ref.cross(p_dir).normalized();
	r_up = p_dir.cross(r_right);
}

String FellowImpostorBaker::shader_code() {
	return R"(
// FellowEngine impostor (decision 0122): a camera-facing quad blending the four nearest of N x N octahedral views.
shader_type spatial;
render_mode cull_disabled, depth_draw_opaque;

uniform sampler2D albedo_atlas : source_color, filter_linear_mipmap, repeat_disable;
uniform sampler2D normal_atlas : filter_linear_mipmap, repeat_disable;
uniform float frames = 8.0;
uniform vec3 center = vec3(0.0);
uniform float radius = 1.0;
uniform float roughness_value = 0.9;
uniform float alpha_scissor = 0.5;

varying vec4 v_uv01;
varying vec4 v_uv23;
varying vec4 v_w;
varying vec3 v_nx;
varying vec3 v_ny;
varying vec3 v_nz;

vec2 octa_encode(vec3 d) {
	d /= abs(d.x) + abs(d.y) + abs(d.z);
	vec2 p = d.xz;
	if (d.y < 0.0) {
		vec2 s = vec2(p.x >= 0.0 ? 1.0 : -1.0, p.y >= 0.0 ? 1.0 : -1.0);
		p = (1.0 - abs(p.yx)) * s;
	}
	return p * 0.5 + 0.5;
}

vec3 octa_decode(vec2 uv) {
	vec2 p = uv * 2.0 - 1.0;
	vec3 d = vec3(p.x, 1.0 - abs(p.x) - abs(p.y), p.y);
	if (d.y < 0.0) {
		vec2 s = vec2(d.x >= 0.0 ? 1.0 : -1.0, d.z >= 0.0 ? 1.0 : -1.0);
		d.xz = (1.0 - abs(d.zx)) * s;
	}
	return normalize(d);
}

void frame_basis(vec3 dir, out vec3 r, out vec3 u) {
	vec3 ref = abs(dir.y) > 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(0.0, 1.0, 0.0);
	r = normalize(cross(ref, dir));
	u = cross(dir, r);
}

// Where the quad's point p (relative to the center) falls in view `cell`'s image.
vec2 frame_uv(vec2 cell, vec3 p) {
	vec3 r;
	vec3 u;
	frame_basis(octa_decode((cell + 0.5) / frames), r, u);
	vec2 l = vec2(dot(p, r), dot(p, u)) / radius;
	return (cell + vec2(l.x * 0.5 + 0.5, 0.5 - l.y * 0.5)) / frames;
}

void vertex() {
	mat4 inv_model = inverse(MODEL_MATRIX);
	vec3 to_cam;
	if (PROJECTION_MATRIX[3][3] > 0.5) {
		to_cam = normalize(mat3(inv_model) * -CAMERA_DIRECTION_WORLD); // orthographic (directional shadows)
	} else {
		to_cam = normalize((inv_model * vec4(CAMERA_POSITION_WORLD, 1.0)).xyz - center);
	}
	vec3 r;
	vec3 u;
	frame_basis(to_cam, r, u);
	vec3 p = (r * VERTEX.x + u * VERTEX.y) * radius;
	VERTEX = center + p;
	NORMAL = to_cam;
	vec2 g = octa_encode(to_cam) * frames - 0.5;
	vec2 b = floor(g);
	vec2 f = g - b;
	vec2 hi = vec2(frames - 1.0);
	v_uv01 = vec4(frame_uv(clamp(b, vec2(0.0), hi), p), frame_uv(clamp(b + vec2(1.0, 0.0), vec2(0.0), hi), p));
	v_uv23 = vec4(frame_uv(clamp(b + vec2(0.0, 1.0), vec2(0.0), hi), p), frame_uv(clamp(b + vec2(1.0, 1.0), vec2(0.0), hi), p));
	v_w = vec4((1.0 - f.x) * (1.0 - f.y), f.x * (1.0 - f.y), (1.0 - f.x) * f.y, f.x * f.y);
	mat3 mv = mat3(MODELVIEW_MATRIX);
	v_nx = mv[0];
	v_ny = mv[1];
	v_nz = mv[2];
}

void fragment() {
	vec4 a0 = texture(albedo_atlas, v_uv01.xy);
	vec4 a1 = texture(albedo_atlas, v_uv01.zw);
	vec4 a2 = texture(albedo_atlas, v_uv23.xy);
	vec4 a3 = texture(albedo_atlas, v_uv23.zw);
	vec4 wa = v_w * vec4(a0.a, a1.a, a2.a, a3.a);
	float alpha = wa.x + wa.y + wa.z + wa.w;
	float inv = 1.0 / max(alpha, 1e-4);
	ALBEDO = (a0.rgb * wa.x + a1.rgb * wa.y + a2.rgb * wa.z + a3.rgb * wa.w) * inv;
	vec3 n = (texture(normal_atlas, v_uv01.xy).rgb * 2.0 - 1.0) * wa.x + (texture(normal_atlas, v_uv01.zw).rgb * 2.0 - 1.0) * wa.y + (texture(normal_atlas, v_uv23.xy).rgb * 2.0 - 1.0) * wa.z + (texture(normal_atlas, v_uv23.zw).rgb * 2.0 - 1.0) * wa.w;
	NORMAL = normalize(mat3(v_nx, v_ny, v_nz) * normalize(n + vec3(0.0, 1e-5, 0.0)));
	ROUGHNESS = roughness_value;
	ALPHA = alpha;
	ALPHA_SCISSOR_THRESHOLD = alpha_scissor;
}
)";
}

void FellowImpostorBaker::queue_bake(const Ref<Mesh> &p_mesh, const Ref<Material> &p_material_override, int p_frames, int p_resolution, const Callable &p_done) {
	ERR_FAIL_COND(p_mesh.is_null());
	Job j;
	j.mesh = p_mesh;
	j.material_override = p_material_override;
	j.frames = CLAMP(p_frames, 2, 32);
	j.resolution = CLAMP(p_resolution, 16, 1024);
	j.done = p_done;
	queue.push_back(j);
	if (!running && is_inside_tree()) {
		_start_next();
	}
}

void FellowImpostorBaker::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_READY: {
			if (!running && !queue.is_empty()) {
				_start_next();
			}
		} break;
		case NOTIFICATION_EXIT_TREE: {
			_cleanup();
			running = false;
		} break;
	}
}

void FellowImpostorBaker::_start_next() {
	if (queue.is_empty()) {
		running = false;
		return;
	}
	const Job &j = queue.front()->get();
	running = true;
	frame = -1;
	warmup = 0;
	const AABB aabb = j.mesh->get_aabb();
	center = aabb.get_center();
	radius = MAX((real_t)1e-3, aabb.size.length() * 0.5);

	world.instantiate();
	Ref<Environment> env;
	env.instantiate();
	env->set_background(Environment::BG_CLEAR_COLOR);
	env->set_ambient_source(Environment::AMBIENT_SOURCE_DISABLED);
	env->set_tonemapper(Environment::TONE_MAPPER_LINEAR);
	world->set_environment(env);

	const int res = j.resolution;
	auto make_vp = [&](Viewport::DebugDraw p_mode) -> SubViewport * {
		SubViewport *vp = memnew(SubViewport);
		vp->set_size(Size2i(res, res));
		vp->set_world_3d(world);
		vp->set_transparent_background(true);
		vp->set_debug_draw(p_mode);
		vp->set_msaa_3d(Viewport::MSAA_DISABLED);
		vp->set_use_taa(false);
		vp->set_mesh_lod_threshold(0.0);
		vp->set_positional_shadow_atlas_size(0);
		vp->set_update_mode(SubViewport::UPDATE_ALWAYS);
		add_child(vp, false, INTERNAL_MODE_BACK);
		return vp;
	};
	auto make_cam = [&](SubViewport *p_vp) -> Camera3D * {
		Camera3D *cam = memnew(Camera3D);
		cam->set_projection(Camera3D::PROJECTION_ORTHOGONAL);
		cam->set_size(2.0 * radius);
		cam->set_near(0.01 * radius);
		cam->set_far(4.0 * radius);
		p_vp->add_child(cam);
		cam->set_current(true);
		return cam;
	};
	vp_albedo = make_vp(Viewport::DEBUG_DRAW_UNSHADED);
	vp_normal = make_vp(Viewport::DEBUG_DRAW_NORMAL_BUFFER);
	cam_albedo = make_cam(vp_albedo);
	cam_normal = make_cam(vp_normal);
	subject = memnew(MeshInstance3D);
	subject->set_mesh(j.mesh);
	subject->set_material_override(j.material_override);
	subject->set_cast_shadows_setting(GeometryInstance3D::SHADOW_CASTING_SETTING_OFF);
	vp_albedo->add_child(subject);

	const int side = j.frames * res;
	atlas_albedo = Image::create_empty(side, side, false, Image::FORMAT_RGBA8);
	atlas_normal = Image::create_empty(side, side, false, Image::FORMAT_RGBA8);
	_place_cameras(0);
	if (!connected) {
		RenderingServer::get_singleton()->connect(SNAME("frame_post_draw"), callable_mp(this, &FellowImpostorBaker::_on_frame_post_draw));
		connected = true;
	}
}

void FellowImpostorBaker::_place_cameras(int p_frame) {
	const Job &j = queue.front()->get();
	const Vector3 dir = frame_direction(p_frame % j.frames, p_frame / j.frames, j.frames);
	Vector3 right, up;
	frame_basis(dir, right, up);
	const Transform3D xf(Basis(right, up, dir), center + dir * (2.0 * radius));
	cam_albedo->set_transform(xf);
	cam_normal->set_transform(xf);
}

void FellowImpostorBaker::_on_frame_post_draw() {
	if (!running || queue.is_empty()) {
		return;
	}
	const Job &j = queue.front()->get();
	if (frame < 0) {
		// Let the viewports render a few frames first (render buffers allocated, pipelines compiled).
		if (++warmup < 4) {
			return;
		}
		frame = 0;
		return; // the view of frame 0 renders in the next frame
	}
	const int res = j.resolution;
	Ref<Image> a = vp_albedo->get_texture()->get_image();
	Ref<Image> n = vp_normal->get_texture()->get_image();
	if (a.is_valid() && n.is_valid() && a->get_width() == res && n->get_width() == res) {
		a->convert(Image::FORMAT_RGBA8);
		n->convert(Image::FORMAT_RGBA8);
		const int fx = frame % j.frames;
		const int fy = frame / j.frames;
		const Vector3 dir = frame_direction(fx, fy, j.frames);
		Vector3 right, up;
		frame_basis(dir, right, up);
		const uint8_t *pa = a->ptr();
		const uint8_t *pn = n->ptr();
		uint8_t *wa = atlas_albedo->ptrw();
		uint8_t *wn = atlas_normal->ptrw();
		const int side = j.frames * res;
		for (int y = 0; y < res; y++) {
			for (int x = 0; x < res; x++) {
				const int s = (y * res + x) * 4;
				const int d = ((fy * res + y) * side + fx * res + x) * 4;
				for (int c = 0; c < 4; c++) {
					wa[d + c] = pa[s + c];
				}
				// The normal buffer holds the view's normal (x right, y up, z towards the camera) as n * 0.5 + 0.5.
				const Vector3 nv(pn[s + 0] / 127.5 - 1.0, pn[s + 1] / 127.5 - 1.0, pn[s + 2] / 127.5 - 1.0);
				Vector3 no = right * nv.x + up * nv.y + dir * nv.z;
				no = no.length_squared() > 1e-8 ? no.normalized() : dir;
				wn[d + 0] = (uint8_t)CLAMP(Math::round((no.x * 0.5 + 0.5) * 255.0), 0.0, 255.0);
				wn[d + 1] = (uint8_t)CLAMP(Math::round((no.y * 0.5 + 0.5) * 255.0), 0.0, 255.0);
				wn[d + 2] = (uint8_t)CLAMP(Math::round((no.z * 0.5 + 0.5) * 255.0), 0.0, 255.0);
				wn[d + 3] = pa[s + 3];
			}
		}
	}
	frame++;
	if (frame >= j.frames * j.frames) {
		_finish();
		return;
	}
	_place_cameras(frame);
}

void FellowImpostorBaker::_finish() {
	Job j = queue.front()->get();
	queue.pop_front();
	const int res = j.resolution;
	const int side = j.frames * res;
	// Colours (and normals) spread into the uncovered texels of each view, so the mipmaps do not darken the edges.
	{
		uint8_t *wa = atlas_albedo->ptrw();
		uint8_t *wn = atlas_normal->ptrw();
		LocalVector<uint8_t> mask;
		mask.resize(side * side);
		for (int i = 0; i < side * side; i++) {
			mask[i] = wa[i * 4 + 3] > 0 ? 1 : 0;
		}
		LocalVector<uint8_t> next;
		for (int it = 0; it < 8; it++) {
			next = mask;
			for (int y = 0; y < side; y++) {
				for (int x = 0; x < side; x++) {
					const int i = y * side + x;
					if (mask[i]) {
						continue;
					}
					int sa[3] = { 0, 0, 0 };
					int sn[3] = { 0, 0, 0 };
					int cnt = 0;
					const int cx = x / res, cy = y / res;
					const int nb[4][2] = { { x - 1, y }, { x + 1, y }, { x, y - 1 }, { x, y + 1 } };
					for (int k = 0; k < 4; k++) {
						const int nx = nb[k][0], ny = nb[k][1];
						if (nx < 0 || ny < 0 || nx >= side || ny >= side || nx / res != cx || ny / res != cy) {
							continue;
						}
						const int ni = ny * side + nx;
						if (!mask[ni]) {
							continue;
						}
						for (int c = 0; c < 3; c++) {
							sa[c] += wa[ni * 4 + c];
							sn[c] += wn[ni * 4 + c];
						}
						cnt++;
					}
					if (cnt > 0) {
						for (int c = 0; c < 3; c++) {
							wa[i * 4 + c] = (uint8_t)(sa[c] / cnt);
							wn[i * 4 + c] = (uint8_t)(sn[c] / cnt);
						}
						next[i] = 1;
					}
				}
			}
			mask = next;
		}
	}
	// Diagnostics: FELLOW_IMPOSTOR_DUMP=<directory> saves each atlas as PNG.
	const String dump = OS::get_singleton()->get_environment("FELLOW_IMPOSTOR_DUMP");
	if (!dump.is_empty()) {
		static int dumped = 0;
		atlas_albedo->save_png(dump.path_join(vformat("impostor_%d_albedo.png", dumped)));
		atlas_normal->save_png(dump.path_join(vformat("impostor_%d_normal.png", dumped)));
		dumped++;
	}
	atlas_albedo->generate_mipmaps();
	atlas_normal->generate_mipmaps();

	if (shader.is_null()) {
		shader.instantiate();
		shader->set_code(shader_code());
	}
	Ref<ShaderMaterial> mat;
	mat.instantiate();
	mat->set_shader(shader);
	mat->set_shader_parameter("albedo_atlas", ImageTexture::create_from_image(atlas_albedo));
	mat->set_shader_parameter("normal_atlas", ImageTexture::create_from_image(atlas_normal));
	mat->set_shader_parameter("frames", (float)j.frames);
	mat->set_shader_parameter("center", center);
	mat->set_shader_parameter("radius", (float)radius);
	float roughness = 1.0;
	Ref<BaseMaterial3D> bm = j.material_override;
	if (bm.is_null() && j.mesh->get_surface_count() > 0) {
		bm = j.mesh->surface_get_material(0);
	}
	if (bm.is_valid()) {
		roughness = bm->get_roughness();
	}
	mat->set_shader_parameter("roughness_value", roughness);

	// The quad: corners (+-1, +-1) moved by the shader; its box bounds the billboard (half size sqrt(2) r) and is centred
	// where the mesh's box is, so the per-instance distance tests judge the same point as for the mesh.
	Ref<ArrayMesh> quad;
	quad.instantiate();
	Array arrays;
	arrays.resize(Mesh::ARRAY_MAX);
	arrays[Mesh::ARRAY_VERTEX] = PackedVector3Array({ Vector3(-1, -1, 0), Vector3(1, -1, 0), Vector3(1, 1, 0), Vector3(-1, 1, 0) });
	arrays[Mesh::ARRAY_NORMAL] = PackedVector3Array({ Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1), Vector3(0, 0, 1) });
	arrays[Mesh::ARRAY_INDEX] = PackedInt32Array({ 0, 1, 2, 0, 2, 3 });
	quad->add_surface_from_arrays(Mesh::PRIMITIVE_TRIANGLES, arrays);
	quad->surface_set_material(0, mat);
	const real_t h = radius * 1.415;
	quad->set_custom_aabb(AABB(center - Vector3(h, h, h), Vector3(2 * h, 2 * h, 2 * h)));

	_cleanup();
	running = false;
	if (j.done.is_valid()) {
		j.done.call(quad);
	}
	_start_next();
}

void FellowImpostorBaker::_cleanup() {
	for (SubViewport **vp : { &vp_albedo, &vp_normal }) {
		if (*vp) {
			(*vp)->queue_free(); // with its camera (and the subject); not while the frame's signal is being sent
			*vp = nullptr;
		}
	}
	cam_albedo = nullptr;
	cam_normal = nullptr;
	subject = nullptr;
	world.unref();
	atlas_albedo.unref();
	atlas_normal.unref();
	if (connected && queue.is_empty()) {
		RenderingServer::get_singleton()->disconnect(SNAME("frame_post_draw"), callable_mp(this, &FellowImpostorBaker::_on_frame_post_draw));
		connected = false;
	}
}

FellowImpostorBaker::~FellowImpostorBaker() {
	if (connected && RenderingServer::get_singleton()) {
		RenderingServer::get_singleton()->disconnect(SNAME("frame_post_draw"), callable_mp(this, &FellowImpostorBaker::_on_frame_post_draw));
		connected = false;
	}
}
