/**************************************************************************/
/*  fellow_hlod_3d.h                                                      */
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

#include "scene/3d/node_3d.h"

class GeometryInstance3D;
class MeshInstance3D;

// FellowEngine automatic HLOD (docs/decisions/0122-hlod.md): beyond a distance, the static meshes under this node are
// replaced by proxies, through Godot's visibility ranges and visibility parents:
// * meshes repeated many times (the same mesh and materials) by one MultiMesh per kind, whose instances the GPU culls
//   one by one (MultiMesh culling, decision 0034) and draws only from the distance on, each mesh node to it: few draws
//   for any number of far objects, switching object by object;
// * the others merged, per cell of a grid and per material, into proxy meshes with generated detail levels that
//   replace their cell's meshes beyond the distance (the proxy is their visibility parent).
class FellowHLOD3D : public Node3D {
	GDCLASS(FellowHLOD3D, Node3D);

public:
	enum ShadowMode {
		SHADOW_MODE_FOLLOW_CAMERA, // stock visibility dependencies in the shadow passes too (merged proxies only)
		SHADOW_MODE_PER_CASCADE, // directional cascades starting beyond the distance draw the proxies, the others the meshes
	};

private:
	real_t distance = 100.0;
	real_t margin = 5.0;
	real_t cell_size = 50.0;
	int min_meshes = 4;
	int instance_threshold = 32;
	bool instancing = true;
	bool lods = true;
	bool build_on_ready = true;
	ShadowMode shadow_mode = SHADOW_MODE_PER_CASCADE;

	struct Member {
		ObjectID id;
		bool instanced = false; // an instanced proxy's member (its own visibility range) or a merged one's (its parent)
		NodePath old_parent;
		float old_range_end = 0.0f;
		float old_range_end_margin = 0.0f;
	};
	LocalVector<Member> members;
	LocalVector<GeometryInstance3D *> merged_proxies;
	LocalVector<GeometryInstance3D *> instanced_proxies;
	Dictionary build_info;
	bool built = false;
	bool build_queued = false;

	void _queue_build();
	void _deferred_build();
	void _apply_roles();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	void set_distance(real_t p_distance);
	real_t get_distance() const;
	void set_margin(real_t p_margin);
	real_t get_margin() const;
	void set_cell_size(real_t p_size);
	real_t get_cell_size() const;
	void set_min_meshes(int p_count);
	int get_min_meshes() const;
	void set_instancing(bool p_enable);
	bool get_instancing() const;
	void set_instance_threshold(int p_count);
	int get_instance_threshold() const;
	void set_generate_lods(bool p_enable);
	bool get_generate_lods() const;
	void set_build_on_ready(bool p_enable);
	bool get_build_on_ready() const;
	void set_shadow_mode(ShadowMode p_mode);
	ShadowMode get_shadow_mode() const;

	void build();
	void clear();
	bool is_built() const { return built; }
	Dictionary get_build_info() const { return build_info; }

	~FellowHLOD3D();
};

VARIANT_ENUM_CAST(FellowHLOD3D::ShadowMode);
