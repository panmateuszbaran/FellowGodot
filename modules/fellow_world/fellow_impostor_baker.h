/**************************************************************************/
/*  fellow_impostor_baker.h                                               */
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

#include "scene/main/node.h"
#include "scene/resources/mesh.h"

class Camera3D;
class MeshInstance3D;
class Shader;
class ShaderMaterial;
class SubViewport;
class World3D;

// FellowEngine impostors (docs/decisions/0122-hlod.md): a mesh rendered from N x N directions over the sphere
// (octahedral mapping) into two atlases — albedo with coverage (the viewport's unshaded debug draw) and the normal (its
// normal buffer, turned from each view's space into the mesh's) — and drawn as a camera-facing quad that blends the
// four nearest views. Baked at run time, one view per frame (internal; used by FellowHLOD3D, and by FellowTerrain3D to
// bake its foliage's impostors into resources, decision 0172).
class FellowImpostorBaker : public Node {
	GDCLASS(FellowImpostorBaker, Node);

public:
	struct Result {
		Ref<ArrayMesh> mesh; // the quad, its box the mesh's bounding sphere's (the culler's distance: the mesh box's center)
		bool ok = false;
	};

private:
	struct Job {
		Ref<Mesh> mesh;
		Ref<Material> material_override;
		int frames = 8;
		int resolution = 128;
		Callable done; // done(ArrayMesh quad with the impostor material, or null)
		bool portable = false; // the atlases as PortableCompressedTexture2D (lossless), for saving
	};
	List<Job> queue;
	bool running = false;
	int frame = -1; // view being rendered; < 0: warming up
	int warmup = 0;
	Vector3 center;
	real_t radius = 1.0;
	Ref<World3D> world;
	SubViewport *vp_albedo = nullptr;
	SubViewport *vp_normal = nullptr;
	Camera3D *cam_albedo = nullptr;
	Camera3D *cam_normal = nullptr;
	MeshInstance3D *subject = nullptr;
	Ref<Shader> shader; // the impostor shader, shared by this baker's materials
	Ref<Image> atlas_albedo;
	Ref<Image> atlas_normal;
	bool connected = false;

	void _start_next();
	void _place_cameras(int p_frame);
	void _on_frame_post_draw();
	void _finish();
	void _cleanup();

protected:
	void _notification(int p_what);

public:
	static Vector3 frame_direction(int p_x, int p_y, int p_frames);
	static void frame_basis(const Vector3 &p_dir, Vector3 &r_right, Vector3 &r_up);
	static String shader_code();

	void queue_bake(const Ref<Mesh> &p_mesh, const Ref<Material> &p_material_override, int p_frames, int p_resolution, const Callable &p_done, bool p_portable = false);
	bool is_busy() const { return running || !queue.is_empty(); }

	~FellowImpostorBaker();
};
