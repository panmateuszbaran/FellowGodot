/**************************************************************************/
/*  fellow_positional_shadow_cache.h                                      */
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

#include "core/templates/safe_refcount.h"
#include "servers/rendering/fellow_settings.h"

// FellowEngine positional shadow static cache (docs/decisions/0373-positional-shadow-cache.md): its settings and
// counters, apart from fellow_settings.h (included by the whole engine through rendering_server.h).
namespace Fellow {

// When an omni or spot light's shadow is drawn again (something in its range moved), the casters that have not
// changed for directional_cache_static_frames frames come from a copy of the face made when they were last drawn, and
// only the others are drawn over it. Each face keeps exactly what stock draws of the static casters (same camera, same
// viewport), so the shadow map is stock's. The copy is made again when the light changes (a light that moves is drawn
// the stock way until it has been still for static_frames), when a cached caster changes or leaves, when casters that
// became static were drawn over the copy for a while, and when the camera moved farther than camera_travel from where
// it was at the copy (the casters' mesh LOD follows the camera; 0: kept until another reason).
inline bool positional_shadow_cache() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/positional_cache");
}
inline float positional_shadow_cache_camera_travel() {
	return MAX(0.0f, GLOBAL_GET_CACHED(float, "rendering/fellow/shadows/positional_cache_camera_travel"));
}
// Verification only: 0 = normal, 1 = make every face again each time it is drawn (the cache path without any reuse).
inline int positional_shadow_cache_debug_mode() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/positional_cache_debug_mode");
}

// The omni and spot lights' shadows drawn again in one render at most this many (0: no limit, stock): when more of
// them need it (something moved in their range), the largest on screen and the longest waiting go first; the others
// keep their shadow of an earlier frame (moving casters' shadows lag) — but a light given a new place in the atlas is
// always drawn, and one that has waited positional_update_max_wait renders is drawn whatever the budget. Read every
// frame. Changes the image while over the budget.
inline int positional_update_budget() {
	return shadows_enabled() ? MAX(0, GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/positional_update_budget")) : 0;
}
inline int positional_update_max_wait() {
	return MAX(1, GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/positional_update_max_wait"));
}

struct PositionalShadowCacheStats {
	SafeNumeric<uint64_t> faces_cached; // positional faces drawn from their cache
	SafeNumeric<uint64_t> faces_rebuilt; // of those, faces whose cache was made again
	SafeNumeric<uint64_t> invalidations; // caches made again because the light or a cached caster changed
	SafeNumeric<uint64_t> promotions; // caches made again to take in casters that became static
	SafeNumeric<uint64_t> camera_rebuilds; // caches made again because the camera moved (mesh LOD)
	SafeNumeric<uint64_t> static_casters; // casters left to the cache (summed over the faces drawn)
	SafeNumeric<uint64_t> dynamic_casters; // casters drawn over the cache (summed over the faces drawn)
	SafeNumeric<uint64_t> budget_drawn; // positional shadows drawn again under the update budget
	SafeNumeric<uint64_t> budget_deferred; // positional shadows the budget left for a later render
	SafeNumeric<uint64_t> budget_forced; // of those drawn, ones that had waited max_wait renders
	SafeNumeric<uint64_t> budget_new_place; // of those drawn, ones given a new place in the atlas
};
PositionalShadowCacheStats &positional_shadow_cache_stats();

} // namespace Fellow
