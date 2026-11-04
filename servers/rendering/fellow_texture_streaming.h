/**************************************************************************/
/*  fellow_texture_streaming.h                                            */
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

#include "core/io/image.h"
#include "core/math/vector3.h"
#include "core/object/worker_thread_pool.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/rid.h"
#include "core/variant/dictionary.h"
#include "servers/rendering/fellow_texture_streaming_types.h"
#include "servers/rendering/rendering_server_types.h"

#include <atomic>

// FellowEngine texture streaming (rendering/fellow/streaming/textures, milestone 45).
//
// A VRAM-compressed CompressedTexture2D with a full mip chain is loaded with only the mips at or below
// texture_resident_size (the "tail"); the file's position of its mip chain is registered here. Every frame the scene
// (RendererSceneCull::fellow_texture_streaming_update) walks a slice of the instances of each scenario a camera
// rendered and reports, for each streamed texture its materials use, the finest mip level the camera can need: from the
// distance of the instance's box, the camera's pixels per metre there, the instance's smallest scale and its surface's
// texel density (the 99th percentile, by area, of the UV mapping's largest stretch, measured when the surface is
// created). When every instance has been seen (a cycle), each texture's target is the finest level any reference
// needed, coarsened uniformly (a global bias) until all of them fit the budget. Loads read the chain from the target
// level on a worker thread and create the new texture there; the render thread swaps it in under the same RID (the
// texture keeps its size, so materials and UVs are unchanged). A texture not used yet keeps its tail; one used where
// the evaluation cannot estimate it (2D, an atlas, a sky or custom shader) is pinned at full resolution (pin_full).
class FellowTextureStreaming {
public:
	typedef FellowStreamMaterialInfo MaterialInfo;

	struct Camera {
		RID scenario;
		Vector3 position;
		Vector3 forward;
		float pixels_per_unit = 0.0f; // at 1 m of view depth for a perspective camera; everywhere for an orthogonal one
		float z_near = 0.05f;
		float mip_bias = 0.0f; // the viewport's texture mipmap bias
		bool orthogonal = false;
	};

private:
	enum State : uint8_t {
		STATE_NEW, // registered, not yet referenced by a 3D material of known UV density
		STATE_STREAMED, // referenced: its target follows the references
		STATE_FULL, // pinned at full resolution: used in 2D, in an atlas, by a material whose UVs are unknown
	};

	struct Job {
		RID texture;
		RID new_texture;
		String path;
		uint64_t file_offset = 0;
		int64_t size = 0;
		int width = 0;
		int height = 0;
		Image::Format format = Image::FORMAT_L8;
		bool mipmaps = false;
		int mip = 0;
		bool downgrade = false;
		WorkerThreadPool::TaskID task = WorkerThreadPool::INVALID_TASK_ID;
		std::atomic<int> result{ 0 }; // 0 running, 1 done, 2 failed
	};

	struct Texture {
		RID rid;
		String path;
		uint64_t data_offset = 0; // the file position of mip 0
		int width = 0;
		int height = 0;
		Image::Format format = Image::FORMAT_L8;
		int levels = 1;
		float log2_size = 0.0f; // log2 of the larger side
		int tail_mip = 0; // always resident: the first level at or below texture_resident_size
		int resident_mip = 0;
		int target_mip = 0;
		int base_target = -1; // the target without the budget's bias, at the last cycle's end
		State state = STATE_NEW;
		uint32_t cycles_seen = 0; // cycles completed since registration
		float cycle_lod = 1e30f; // the finest LOD any reference of the current cycle needed (1e30: none)
		float lod = 1e30f; // the last completed cycle's
		bool cycle_full = false; // a reference of the current cycle needs full resolution
		uint64_t unreferenced_since_usec = 0;
		uint64_t coarser_since_usec = 0; // target coarser than resident since
		Job *job = nullptr;
		int64_t mip_offsets[17] = {}; // byte offset of each level in the chain; [levels] is the chain's size
		int64_t chain_size(int p_mip) const { return mip_offsets[levels] - mip_offsets[p_mip]; }
	};

	struct MaterialCache {
		MaterialInfo info;
		LocalVector<Texture *> streamed; // info.textures that are streamed, resolved at `epoch`
		uint64_t epoch = 0;
		uint64_t checked_frame = 0; // its version was checked in this update
	};

	HashMap<RID, Texture *> textures;
	HashMap<RID, MaterialCache> materials;
	uint64_t textures_epoch = 1; // bumped when a texture is registered or freed
	uint64_t frame = 0;
	RID last_material;
	MaterialCache *last_cache = nullptr;
	bool schedule_dirty = true;
	int waiting_releases = 0;
	LocalVector<Camera> cameras; // recorded this frame (render_camera)
	LocalVector<Camera> eval_cameras; // the previous frame's, used by the evaluation
	LocalVector<Job *> jobs;

	int64_t resident_bytes = 0;
	int64_t peak_resident_bytes = 0;
	int max_pending = 0;
	int64_t target_bytes = 0;
	int64_t wanted_bytes = 0; // the targets without the budget's bias
	int64_t full_bytes = 0;
	float budget_bias = 0.0f;
	uint64_t cycles = 0;
	uint64_t loads = 0;
	uint64_t downgrades = 0;
	uint64_t failures = 0;
	uint64_t bytes_read = 0;
	uint64_t eval_usec = 0;
	uint64_t eval_references = 0;
	bool targets_valid = false;
	int64_t computed_budget = -1; // the budget the targets were computed for

	struct Stats {
		int textures = 0, streamed = 0, full = 0, fresh = 0, at_full = 0, at_tail = 0, pending = 0, jobs = 0, materials = 0;
		int64_t resident_bytes = 0, peak_resident_bytes = 0, target_bytes = 0, wanted_bytes = 0, full_bytes = 0;
		int max_pending = 0;
		float budget_bias = 0.0f;
		uint64_t cycles = 0, loads = 0, downgrades = 0, failures = 0, bytes_read = 0, eval_usec = 0, eval_references = 0, update_usec = 0, frames = 0;
	};
	Stats counts; // the render thread's, from the last schedule
	mutable Mutex info_mutex;
	Stats info_stats; // copied under the mutex every update
	Array info_details;
	uint64_t update_usec = 0;

	static void _job_run(void *p_job);
	void _finish_job(Texture *p_tex, Job *p_job);
	static int _target_at(const Texture *p_tex, float p_bias);
	static int _base_target(const Texture *p_tex) { return _target_at(p_tex, 0.0f); }
	void _compute_targets();
	void _schedule();
	void _start_job(Texture *p_tex, int p_mip, bool p_downgrade);
	void _update_snapshot();

	static FellowTextureStreaming *singleton;

public:
	static FellowTextureStreaming *get_singleton();
	static void destroy_singleton(); // RenderingServer's finish: jobs waited for, their textures freed

	// Read once at start-up (the loads and the meshes' texel densities depend on it).
	static bool enabled_at_startup();
	static int resident_size();

	// The texel density (UV units per mesh unit, the 99th percentile by area of each triangle's largest stretch) of a
	// surface's first UV channel; 0 if it has none or is not made of triangles.
	static float compute_uv_density(const RenderingServerTypes::SurfaceData &p_surface);

	bool has_textures() const { return !textures.is_empty(); }

	// Render thread.
	void register_texture(RID p_texture, const String &p_path, uint64_t p_data_offset, int p_width, int p_height, Image::Format p_format, int p_levels, int p_resident_mip);
	void texture_freed(RID p_texture);
	// A use the streamer cannot estimate (2D, an atlas, a sky, a custom shader): full resolution from now on.
	void pin_full(RID p_texture);
	void add_camera(const Camera &p_camera);
	const LocalVector<Camera> &get_eval_cameras() const { return eval_cameras; }
	void begin_frame(); // moves this frame's cameras to the evaluation
	// One reference: a material of a surface with p_density texels per... (see the .cpp), seen at p_pixels_per_unit
	// pixels per world unit, the instance's smallest scale p_scale.
	void reference_material(RID p_material, float p_pixels_per_unit, float p_scale, float p_density, float p_mip_bias);
	void end_cycle();
	void update(); // jobs, targets, scheduling (after the evaluation)
	void finish();

	uint64_t get_cycles() const { return cycles; }
	void add_eval_time(uint64_t p_usec, uint64_t p_refs) {
		eval_usec += p_usec;
		eval_references += p_refs;
	}

	// Any thread.
	Dictionary get_info() const;
};
