/**************************************************************************/
/*  fellow_depth_fit.h                                                    */
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
#include "core/templates/safe_refcount.h"
#include "servers/rendering/fellow_settings.h"

// FellowEngine depth-fitted sun shadows (docs/decisions/0388-directional-depth-fit.md): the range of depths a view
// actually shows (its depth pre-pass reduced on the GPU, read back a few frames late) narrows the directional light's
// cascades to it, so that their texels are spent on what is seen (sample distribution shadow maps, Lauritzen et al.).
// Apart from fellow_settings.h (included by the whole engine through rendering_server.h).
namespace Fellow {

// On: the directional shadows' cascades span the depths the view showed a few frames ago (with a margin) instead of
// the camera's near plane to the shadow's max distance. Changes the image (sharper shadows where the view ends short
// of the max distance). Read every frame.
inline bool directional_depth_fit() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/directional_depth_fit");
}

// A view's depths: the nearest and the farthest opaque surface (metres from the camera, along its axis), the frame
// they were rendered in. Kept per render buffers (one per viewport), written by the renderer's readback, read by the
// scene cull when it sets the cascades up.
struct DepthFitRange {
	float nearest = 0.0f;
	float farthest = 0.0f;
	uint64_t frame = 0;
	bool any = false; // an opaque surface was seen (else: only sky)
	Vector3 cam_origin; // the camera the depths were rendered from
	Vector3 cam_forward;
	// Held: the farthest of the recent readbacks, letting go by DEPTH_FIT_RELEASE a readback (and the nearest
	// likewise): turning back and forth does not narrow and widen the cascades every time.
	float far_held = 0.0f;
	float near_held = 0.0f;
};

static constexpr float DEPTH_FIT_RELEASE = 0.97f;

// Counters (fellow_get_stats()'s shadows_depth_fit_*): renders whose cascades were fitted, and the last fit's range.
struct DepthFitStats {
	SafeNumeric<uint64_t> fitted_renders;
	SafeNumeric<uint64_t> last_near_mm;
	SafeNumeric<uint64_t> last_far_mm;
};

inline DepthFitStats &depth_fit_stats() {
	static DepthFitStats stats;
	return stats;
}

struct DepthFitStore {
	Mutex mutex;
	HashMap<ObjectID, DepthFitRange> ranges;
};

inline DepthFitStore &depth_fit_store() {
	static DepthFitStore store;
	return store;
}

// A readback's range, held with the earlier ones (an older frame's arriving after a newer one's is dropped).
inline void depth_fit_set(ObjectID p_buffers, const DepthFitRange &p_range) {
	DepthFitStore &s = depth_fit_store();
	MutexLock lock(s.mutex);
	if (s.ranges.size() > 64 && !s.ranges.has(p_buffers)) {
		s.ranges.clear(); // (render buffers freed long ago)
	}
	DepthFitRange r = p_range;
	const DepthFitRange *old = s.ranges.getptr(p_buffers);
	if (old != nullptr && old->frame > r.frame) {
		return;
	}
	if (r.any) {
		const bool hold = old != nullptr && old->any;
		r.far_held = hold ? MAX(r.farthest, old->far_held * DEPTH_FIT_RELEASE) : r.farthest;
		r.near_held = hold ? MIN(r.nearest, old->near_held / DEPTH_FIT_RELEASE) : r.nearest;
	}
	s.ranges[p_buffers] = r;
}

inline bool depth_fit_get(ObjectID p_buffers, DepthFitRange &r_range) {
	DepthFitStore &s = depth_fit_store();
	MutexLock lock(s.mutex);
	const DepthFitRange *r = s.ranges.getptr(p_buffers);
	if (r == nullptr) {
		return false;
	}
	r_range = *r;
	return true;
}

} // namespace Fellow
