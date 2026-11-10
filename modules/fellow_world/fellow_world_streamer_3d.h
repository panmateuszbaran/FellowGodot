/**************************************************************************/
/*  fellow_world_streamer_3d.h                                            */
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

#include "core/object/worker_thread_pool.h"
#include "core/templates/hash_set.h"
#include "scene/3d/node_3d.h"
#include "scene/resources/mesh.h"
#include "scene/resources/packed_scene.h"

#include <atomic>

class FellowWorldStreamer3D;

// FellowEngine world streaming (milestone 46): a cell of a level, its content a PackedScene loaded when a camera (or a
// target of its FellowWorldStreamer3D) comes within its load distance and freed when it goes beyond it plus the
// streamer's margin. The cell's own children (a proxy: an HLOD mesh, a low-detail stand-in) are hidden while the
// content is in the tree.
class FellowStreamingCell3D : public Node3D {
	GDCLASS(FellowStreamingCell3D, Node3D);

public:
	enum State {
		STATE_UNLOADED,
		STATE_LOADING, // the resource loads on the ResourceLoader's threads
		STATE_INSTANCING, // its nodes are created on a worker thread
		STATE_READY, // instantiated, waiting for its turn to enter the tree
		STATE_LOADED, // in the tree
	};

private:
	friend class FellowWorldStreamer3D;

	String scene_path;
	AABB bounds = AABB(Vector3(-32, -16, -32), Vector3(64, 64, 64));
	real_t load_distance = 0.0; // 0: the streamer's
	State state = STATE_UNLOADED;
	Node *content = nullptr;
	FellowWorldStreamer3D *streamer = nullptr;
	uint64_t request_usec = 0;
	float distance = 0.0f; // the streamer's last measure
	Node *editor_preview = nullptr; // milestone 50: the content shown in the editor (internal, not saved)

	void _set_proxy_visible(bool p_visible);
	FellowWorldStreamer3D *_find_streamer() const;

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;

	void set_scene_path(const String &p_path);
	String get_scene_path() const { return scene_path; }
	void set_bounds(const AABB &p_bounds);
	AABB get_bounds() const { return bounds; }
	void set_load_distance(real_t p_distance) {
		load_distance = p_distance;
		update_gizmos(); // milestone 52
	}
	real_t get_load_distance() const { return load_distance; }

	State get_state() const { return state; }
	bool is_loaded() const { return state == STATE_LOADED; }
	Node *get_content() const { return content; }
	AABB get_global_bounds() const;

	// Milestone 50 (tools): in the editor, the content as an internal child that is not saved with the level, where
	// the cell's FellowWorldStreamer3D has editor_preview on; nothing in a game.
	void _update_editor_preview();
	void _editor_hide_proxies();

	FellowStreamingCell3D() {}
	~FellowStreamingCell3D();
};

// FellowEngine world streaming (milestone 46): keeps the FellowStreamingCell3D under it loaded around the viewport's
// camera and its targets. Loads run on the ResourceLoader's threads (a few at once), the cells' nodes are created on
// a worker thread, and at most a few cells a frame enter or leave the tree, the nearest first in, the farthest first
// out. With FellowEngine's texture streaming the cells' textures arrive with only their small mips, and the rest as
// the camera needs it.
class FellowWorldStreamer3D : public Node3D {
	GDCLASS(FellowWorldStreamer3D, Node3D);

	real_t load_distance = 200.0;
	real_t unload_margin = 25.0;
	int max_loading = 2;
	int max_activations_per_frame = 1;
	int max_releases_per_frame = 2;
	bool threaded_instancing = true;
	bool streaming = true;
	bool use_camera = true;
	bool editor_preview = true; // milestone 50
	TypedArray<NodePath> targets;
	LocalVector<Vector3> prefetch_points;
	real_t prefetch_radius = 0.0;

	LocalVector<FellowStreamingCell3D *> cells;

	struct Instancing {
		ObjectID cell;
		Ref<PackedScene> scene;
		Node *result = nullptr;
		WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
		std::atomic<bool> done{ false };
		uint64_t usec = 0;
	};
	LocalVector<Instancing *> instancing;

	// Statistics.
	uint64_t loads = 0;
	uint64_t releases = 0;
	uint64_t failures = 0;
	uint64_t activation_usec = 0; // main thread: entering the tree
	uint64_t activation_usec_max = 0;
	uint64_t release_usec = 0; // main thread: leaving the tree
	uint64_t release_usec_max = 0;
	uint64_t instancing_usec = 0; // worker threads
	uint64_t latency_usec = 0; // request to in the tree
	uint64_t latency_usec_max = 0;
	uint64_t update_usec = 0;
	uint64_t frames = 0;

	static LocalVector<FellowWorldStreamer3D *> in_tree; // milestone 50: for get_totals()

	// Decision 0174: streamed meshes — stand-ins in the cells' content (bake_proxies() with "stream_meshes"), their
	// full meshes loaded on the ResourceLoader's threads within mesh_distance and dropped beyond it.
	real_t mesh_distance = 0.0; // 0: the full meshes whenever their cell is loaded
	int max_mesh_loads = 2;
	enum MeshState : uint8_t {
		MESH_STAND_IN,
		MESH_LOADING,
		MESH_FULL,
		MESH_FAILED,
	};
	struct StreamedMesh {
		ObjectID node;
		FellowStreamingCell3D *cell = nullptr;
		String path;
		Ref<Mesh> stand_in;
		Ref<Mesh> full;
		MeshState state = MESH_STAND_IN;
	};
	LocalVector<StreamedMesh> streamed_meshes;
	HashSet<String> mesh_pending; // paths requested from the ResourceLoader
	uint64_t mesh_loads = 0;
	uint64_t mesh_releases = 0;
	uint64_t mesh_swap_usec = 0;
	uint64_t mesh_swap_usec_max = 0;
	void _register_meshes(FellowStreamingCell3D *p_cell, Node *p_content);
	void _unregister_meshes(FellowStreamingCell3D *p_cell);
	void _update_meshes(const LocalVector<Vector3> &p_points);

	static void _instance_task(void *p_data);
	void _update();
	void _flush_worker_commands();
	void _gather_points(LocalVector<Vector3> &r_points) const;
	void _activate(FellowStreamingCell3D *p_cell, Node *p_content);
	void _release(FellowStreamingCell3D *p_cell);
	void _cancel_all();

protected:
	void _notification(int p_what);
	static void _bind_methods();

public:
	PackedStringArray get_configuration_warnings() const override;

	void _register_cell(FellowStreamingCell3D *p_cell);
	void _unregister_cell(FellowStreamingCell3D *p_cell);

	void set_load_distance(real_t p_distance) { load_distance = MAX(p_distance, real_t(0.0)); }
	real_t get_load_distance() const { return load_distance; }
	void set_unload_margin(real_t p_margin) { unload_margin = MAX(p_margin, real_t(0.0)); }
	real_t get_unload_margin() const { return unload_margin; }
	void set_max_loading(int p_count) { max_loading = CLAMP(p_count, 1, 64); }
	int get_max_loading() const { return max_loading; }
	void set_max_activations_per_frame(int p_count) { max_activations_per_frame = CLAMP(p_count, 1, 1024); }
	int get_max_activations_per_frame() const { return max_activations_per_frame; }
	void set_max_releases_per_frame(int p_count) { max_releases_per_frame = CLAMP(p_count, 1, 1024); }
	int get_max_releases_per_frame() const { return max_releases_per_frame; }
	void set_threaded_instancing(bool p_enable) { threaded_instancing = p_enable; }
	bool is_threaded_instancing() const { return threaded_instancing; }
	void set_streaming(bool p_enable);
	bool is_streaming() const { return streaming; }
	void set_editor_preview(bool p_enable);
	bool is_editor_preview() const { return editor_preview; }
	void set_use_camera(bool p_enable) {
		use_camera = p_enable;
		update_configuration_warnings();
	}
	bool is_using_camera() const { return use_camera; }
	void set_targets(const TypedArray<NodePath> &p_targets) {
		targets = p_targets;
		update_configuration_warnings();
	}
	TypedArray<NodePath> get_targets() const { return targets; }

	// Cells around a point load as if a camera were there (a teleport's destination, behind a loading screen).
	void prefetch(const Vector3 &p_position, real_t p_radius);
	void clear_prefetch();
	// Every cell that should be in the tree is, and nothing is loading.
	bool is_settled() const;
	// Loads and activates synchronously every cell in range now (a loading screen); returns how many entered.
	int load_now();

	void set_mesh_distance(real_t p_distance) { mesh_distance = MAX(p_distance, real_t(0.0)); }
	real_t get_mesh_distance() const { return mesh_distance; }
	void set_max_mesh_loads(int p_count) { max_mesh_loads = CLAMP(p_count, 1, 64); }
	int get_max_mesh_loads() const { return max_mesh_loads; }
	// Decision 0174: for each cell, a proxy baked from its content (merged, simplified, coloured by its materials) saved in
	// p_directory and added under the cell, shown while the cell is not loaded; with "stream_meshes", its large meshes'
	// stand-ins written into a copy of its scene, their full meshes saved apart and streamed by mesh_distance. Returns
	// the bake's counts.
	Dictionary bake_proxies(const String &p_directory, const Dictionary &p_options = Dictionary());
	Dictionary get_stats() const;

	// Milestone 50 (tools): every streamer in the tree's cells, summed, for the Performance monitors (FellowEngine's
	// "World cells loaded" / "World cells loading"). Main thread.
	static void get_totals(int &r_cells, int &r_loaded, int &r_loading);

	// Milestone 50 (tools): splits p_root's 3D children into cells of p_cell_size metres on this streamer's XZ grid,
	// each cell's nodes saved as a PackedScene in p_directory and replaced by a FellowStreamingCell3D under this
	// streamer. Options: "exclude_classes" (classes left where they are: cameras, directional lights, environments,
	// streamers and cells by default), "max_span" (a node wider than this many cells stays, 2), "load_distance" (the
	// cells', 0: the streamer's), "extension" ("scn" or "tscn"), "owner" (the new cells' owner; the root's owner by
	// default, so that the editor saves them), "split_terrains" (milestone 65: a FellowTerrain3D split into regions of
	// about a cell's size, each a cell, the terrain removed; false). Returns the cells made.
	Array split_into_cells(Node3D *p_root, real_t p_cell_size, const String &p_directory, const Dictionary &p_options = Dictionary());

	FellowWorldStreamer3D();
	~FellowWorldStreamer3D();
};

VARIANT_ENUM_CAST(FellowStreamingCell3D::State);
