/**************************************************************************/
/*  fellow_virtual_shadow.h                                               */
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

#include "core/math/vector3.h"
#include "core/object/object_id.h"
#include "core/os/mutex.h"
#include "core/templates/hash_map.h"
#include "core/templates/local_vector.h"
#include "core/templates/paged_array.h"
#include "core/templates/rid.h"
#include "core/templates/safe_refcount.h"
#include "servers/rendering/fellow_settings.h"

class RenderGeometryInstance;

// FellowEngine virtual shadow maps (docs/decisions/0396-virtual-shadow-maps.md): the first directional light's shadow
// as clipmap levels of 128 x 128 texel pages, each level's texels twice the size of the one before, around the camera;
// only the pages the view's pixels look up are allocated in a physical pool and drawn, and they are kept (cached) until
// something in them changes. The view's depth pre-pass marks the pages its pixels need (on the GPU, read back a few
// frames late); the scene cull allocates them, finds what must be drawn again, and hands the renderer one pass per
// level with pages to draw. Apart from fellow_settings.h (included by the whole engine through rendering_server.h).
namespace Fellow {

static constexpr int VSM_PAGE = 128; // texels per page side
static constexpr int VSM_WINDOW = 64; // pages per level side (a level spans 8192 texels around the camera)
static constexpr int VSM_MAX_LEVELS = 16;
static constexpr int VSM_REGION = 16; // pages per pass side at most (a pass draws at most 2048 x 2048 texels)
static constexpr float VSM_DEPTH_SCALE = 1024.0f; // the pool's depths: heights towards the light in 1/1024 m
static constexpr uint32_t VSM_ENTRY_DRAWN = 1u << 30; // a page table entry's page is drawn this frame
static constexpr uint32_t VSM_ENTRY_PHYS_MASK = 0x00ffffffu;

// On: the first shadowed directional light's shadow from the virtual shadow map where its pages are resident, from
// its cascades elsewhere. Changes the image (shadows sharp at every distance). Read every frame.
inline bool vsm_enabled() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/virtual/enabled");
}

inline int vsm_levels() {
	return CLAMP((int)GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/virtual/levels"), 1, VSM_MAX_LEVELS);
}

// The finest level's texel (metres); level l's is this times 2^l.
inline float vsm_texel_size() {
	return MAX(0.0001f, (float)GLOBAL_GET_CACHED(float, "rendering/fellow/shadows/virtual/texel_size"));
}

// The physical pool's side in texels (a multiple of the page's): (side / 128)^2 pages, 4 bytes a texel.
inline int vsm_pool_size() {
	const int s = CLAMP((int)GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/virtual/pool_size"), 1024, 16384);
	return (s / VSM_PAGE) * VSM_PAGE;
}

// Levels added to the one a pixel's footprint asks for (> 0: coarser, fewer pages).
inline float vsm_lod_bias() {
	return (float)GLOBAL_GET_CACHED(float, "rendering/fellow/shadows/virtual/lod_bias");
}

// At most this many pages allocated (and drawn for the first time) a frame.
inline int vsm_max_new_pages() {
	return MAX(1, (int)GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/virtual/max_new_pages_per_frame"));
}

// What the scene cull hands the renderer for one view and frame.
struct VSMFrame {
	bool active = false;
	RID light_instance;
	uint64_t frame = 0;
	uint32_t state_id = 0; // a new id: the pool's content is not the state's (nothing resident was drawn into it)
	int levels = 0;
	float texel0 = 0.0f;
	float lod_bias = 0.0f;
	float max_distance = 0.0f; // beyond it, no shadow (the light's)
	int pool_pages = 0; // per side
	// Light space: x and y across the light, z towards it.
	Vector3 axis_x, axis_y, axis_z;
	// The passes' cameras sit at h_top (light-space z) and see down to h_bottom (casters above h_top are flattened onto
	// it, as stock's shadows pancake them).
	double h_top = 0.0;
	double h_bottom = 0.0;
	int32_t window[VSM_MAX_LEVELS][2] = {}; // each level's first page (absolute page coordinates)
	double window_origin[VSM_MAX_LEVELS][2] = {}; // its corner across the light (m)
	float origin[VSM_MAX_LEVELS][2] = {}; // the camera in each level's window (its texels)
	Vector3 cam_origin; // the view's camera
	// Heights towards the light in the pool's fixed point (VSM_DEPTH_SCALE): the passes' top, the camera's (and the
	// fraction rounded away).
	int32_t h_top_fixed = 0;
	int32_t cam_h_fixed = 0;
	float cam_h_frac = 0.0f;
	// levels * VSM_WINDOW^2 entries: 0 none; else the physical page + 1, | VSM_ENTRY_DRAWN if drawn this frame.
	LocalVector<uint32_t> table;
	LocalVector<uint32_t> clear_pages; // physical pages drawn this frame (cleared before)
	// A pass draws the casters of a rectangle of one level's window (window page coordinates, at most VSM_REGION
	// pages a side) into its pages drawn this frame.
	struct Pass {
		int level = 0;
		int32_t page_min[2] = {};
		int32_t page_max[2] = {}; // inclusive
		PagedArray<RenderGeometryInstance *> *instances = nullptr;
	};
	LocalVector<Pass> passes;
	// Counters for fellow_get_stats().
	uint32_t resident = 0;
	uint32_t drawn = 0;
};

// The pages a view's pixels asked for, read back from the GPU.
struct VSMRequests {
	uint64_t frame = 0;
	uint32_t state_id = 0;
	int levels = 0;
	float footprint = 0.0f; // a pixel's size per metre of depth the pages were marked with
	int32_t window[VSM_MAX_LEVELS][2] = {};
	Vector<uint8_t> bits; // levels * VSM_WINDOW^2 bits, level by level, row by row
};

struct VSMStore {
	Mutex mutex;
	HashMap<ObjectID, VSMRequests> requests;
	HashMap<ObjectID, uint8_t> lost; // render buffers whose pool no longer holds what their state thinks is resident
};

inline VSMStore &vsm_store() {
	static VSMStore store;
	return store;
}

// A readback's requests (an older frame's arriving after a newer one's is dropped).
inline void vsm_requests_set(ObjectID p_buffers, const VSMRequests &p_requests) {
	VSMStore &s = vsm_store();
	MutexLock lock(s.mutex);
	if (s.requests.size() > 64 && !s.requests.has(p_buffers)) {
		s.requests.clear(); // (render buffers freed long ago)
	}
	const VSMRequests *old = s.requests.getptr(p_buffers);
	if (old != nullptr && old->frame > p_requests.frame) {
		return;
	}
	s.requests[p_buffers] = p_requests;
}

// The latest requests not taken yet.
inline bool vsm_requests_take(ObjectID p_buffers, VSMRequests &r_requests) {
	VSMStore &s = vsm_store();
	MutexLock lock(s.mutex);
	VSMRequests *r = s.requests.getptr(p_buffers);
	if (r == nullptr || r->bits.is_empty()) {
		return false;
	}
	r_requests = *r;
	r->bits.clear();
	return true;
}

// The renderer found a pool that does not hold its state's pages (made again: the render buffers were resized, the
// pool's size changed): the scene cull starts a new state for these render buffers.
inline void vsm_pool_lost(ObjectID p_buffers) {
	VSMStore &s = vsm_store();
	MutexLock lock(s.mutex);
	if (s.lost.size() > 64) {
		s.lost.clear();
	}
	s.lost[p_buffers] = 1;
}

inline bool vsm_pool_lost_take(ObjectID p_buffers) {
	VSMStore &s = vsm_store();
	MutexLock lock(s.mutex);
	return s.lost.erase(p_buffers);
}

struct VSMStats {
	SafeNumeric<uint64_t> resident;
	SafeNumeric<uint64_t> drawn;
	SafeNumeric<uint64_t> passes;
	SafeNumeric<uint64_t> requested;
	SafeNumeric<uint64_t> update_usec; // the scene cull's part, the last frame
};

inline VSMStats &vsm_stats() {
	static VSMStats stats;
	return stats;
}

} // namespace Fellow
