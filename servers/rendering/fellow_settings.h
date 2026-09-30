/**************************************************************************/
/*  fellow_settings.h                                                     */
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

#include "core/config/project_settings.h"
#include "core/templates/safe_refcount.h"

// FellowEngine runtime feature toggles.
//
// All FellowEngine rendering changes are gated behind project settings under rendering/fellow/*.
// Defaults reproduce stock Godot 4.7.2 behaviour exactly (every experimental feature is off), so a
// FellowEngine binary with default settings renders like stock and the benchmark can A/B at runtime.
// Values are read through GLOBAL_GET_CACHED, i.e. they may be changed while the game is running.
namespace Fellow {

void register_settings();

inline bool enabled() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/enabled");
}

inline bool postfx_enabled() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/enabled");
}

// TAA resolve v2: reverse-Z aware closest-depth dilation, history reprojection with the dilated velocity,
// YCoCg variance clipping in a tonemapped space, feedback weighting, NaN guard. See docs/decisions/.
inline bool taa_resolve_v2() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/taa_resolve_v2");
}

// TAA history ping-pong: the resolve writes the new history and next-frame velocity directly,
// removing two full-resolution copies per frame. Output is identical to the copy-based path.
inline bool taa_history_pingpong() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/taa_history_pingpong");
}

// Captures the engine's RENDER_TIMESTAMP markers every frame (like --gpu-profile, without printing), so that
// per-pass GPU timings are available to scripts (RenderingDevice.get_captured_timestamp_*) and the overlay.
// Timestamps act as ordering fences in the render graph: keep this off for performance measurements.
inline bool pass_timestamps() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/profiling/pass_timestamps");
}

inline bool fluid_enabled() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/fluid/enabled");
}

inline bool shadows_enabled() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/enabled");
}

// Directional shadow static cache (docs/decisions/0004-directional-shadow-cache.md): static casters of every
// cascade are rendered into a cache with a guard band and only re-rendered when the cascade leaves the guard band,
// the light rotates or a cached caster changes; each frame the cache is copied into the atlas (depth re-based to the
// cascade's exact stock projection) and only dynamic casters are drawn on top.
inline bool directional_shadow_cache() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/directional_cache");
}

// Guard band in texels on each side of a cascade (cache texture = cascade size + 2 * guard).
inline int directional_shadow_cache_guard() {
	return CLAMP(GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/directional_cache_guard_texels"), 16, 1024);
}

// Frames an instance must stay unchanged before it is treated as static and cached.
inline int directional_shadow_cache_static_frames() {
	return MAX(1, GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/directional_cache_static_frames"));
}

// Maximum camera travel (m) while a cascade stays cached; cached casters use a LOD chosen for a camera this much
// closer, so the cache is never coarser than the stock per-frame LOD choice.
inline float directional_shadow_cache_lod_travel() {
	return MAX(0.0f, GLOBAL_GET_CACHED(float, "rendering/fellow/shadows/directional_cache_lod_travel"));
}

// Cache re-renders allowed per frame; cascades that need one beyond the budget are drawn the stock way that frame.
inline int directional_shadow_cache_max_rebuilds() {
	return MAX(1, GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/directional_cache_max_rebuilds_per_frame"));
}

// Verification only: 0 = normal, 1 = cascade lattice without using the cache (stock rendering of every cascade),
// 2 = rebuild every cascade every frame (exercises the cache path without any reuse).
inline int directional_shadow_cache_debug_mode() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/directional_cache_debug_mode");
}

// Cumulative counters (render thread writes, scripts read through RenderingServer.fellow_get_stats()).
struct Stats {
	SafeNumeric<uint64_t> shadow_cache_cascades_stock;
	SafeNumeric<uint64_t> shadow_cache_cascades_fallback;
	SafeNumeric<uint64_t> shadow_cache_cascades_cached;
	SafeNumeric<uint64_t> shadow_cache_cascades_rebuilt;
	SafeNumeric<uint64_t> shadow_cache_rebuilds_forced;
	SafeNumeric<uint64_t> shadow_cache_rebuilds_proactive;
	SafeNumeric<uint64_t> shadow_cache_rebuilds_promotion;
	SafeNumeric<uint64_t> shadow_cache_invalidations;
	SafeNumeric<uint64_t> shadow_cache_dynamic_casters;
	SafeNumeric<uint64_t> shadow_cache_rebuild_casters;
	SafeNumeric<uint64_t> shadow_cache_frames;
};

Stats &stats();

} // namespace Fellow

class Dictionary;
namespace Fellow {
Dictionary get_stats_dictionary();

} // namespace Fellow
