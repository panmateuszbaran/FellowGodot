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
// Defaults are the FellowEngine configuration: every feature whose decision record says KEEP is on (TAA resolve v2
// with history ping-pong and motion soften, directional shadow cache, exact shadow sampling skips, mesh clusters);
// look changes and rejected experiments are off. rendering/fellow/enabled = false reproduces stock Godot 4.7.2
// exactly (null test), and every toggle can be changed per project or at runtime for A/B runs.
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

// Resolve v2 tunables (defaults = the milestone-1 resolve v2).
inline float taa_v2_gamma() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/postfx/taa_v2_gamma");
}
inline float taa_v2_feedback_min() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/postfx/taa_v2_feedback_min");
}
inline float taa_v2_feedback_max() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/postfx/taa_v2_feedback_max");
}
inline float taa_v2_antiflicker() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/postfx/taa_v2_antiflicker");
}
// Resolve v2: reproject the history with the velocity of the closest pixel in 3x3 (1, default: silhouettes of moving
// objects carry their history) or with the pixel's own velocity (0, like stock: no misregistration of background
// pixels between foliage layers moving with different parallax).
inline bool taa_v2_velocity_dilation() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/taa_v2_velocity_dilation");
}
// Resolve v2: under motion, pull the history towards the 3x3 mean where the neighbourhood has high relative contrast
// (sub-pixel alpha-tested content). 0 = off.
inline float taa_v2_motion_soften() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/postfx/taa_v2_motion_soften");
}
inline float taa_v2_soften_contrast() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/postfx/taa_v2_soften_contrast");
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

// Mesh LOD of cached casters. texel_lod > 0: chosen from the cascade's own texel size (the coarsest LOD whose error is
// below texel_lod shadow-map texels; geometric detail below a texel cannot show in the shadow map). texel_lod = 0:
// chosen like stock (camera distance at the time of the rebuild); with lod_travel > 0 as if the camera were that much
// closer, and the cache is invalidated once the camera moved farther than that (never coarser than stock).
inline float directional_shadow_cache_texel_lod() {
	return MAX(0.0f, GLOBAL_GET_CACHED(float, "rendering/fellow/shadows/directional_cache_texel_lod"));
}
inline float directional_shadow_cache_lod_travel() {
	return MAX(0.0f, GLOBAL_GET_CACHED(float, "rendering/fellow/shadows/directional_cache_lod_travel"));
}

// Cache re-renders allowed per frame; cascades that need one beyond the budget are drawn the stock way that frame.
inline int directional_shadow_cache_max_rebuilds() {
	return MAX(1, GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/directional_cache_max_rebuilds_per_frame"));
}

// Cost gate: a cascade is served from its cache only if the static casters it saves cost more than the copy.
// Each cache rebuild measures its casters in primitive equivalents (primitives + 2000 per draw); the frame after, the
// cull counts how many of them stock would draw (camera-dependent tight culling), which scales the measured cost to
// what stock spends on static casters per frame. The copy writes one depth texel per cascade texel. A cascade whose
// estimated static cost per cascade texel is below this value is rendered the stock way (re-evaluated when it leaves
// the evaluated volume, or after 600 frames). 0 disables the gate.
inline float directional_shadow_cache_min_cost() {
	return MAX(0.0f, GLOBAL_GET_CACHED(float, "rendering/fellow/shadows/directional_cache_min_cost_per_texel"));
}

// Verification only: 0 = normal, 1 = cascade lattice without using the cache (stock rendering of every cascade),
// 2 = rebuild every cascade every frame (exercises the cache path without any reuse).
inline int directional_shadow_cache_debug_mode() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/shadows/directional_cache_debug_mode");
}

// Diagnostics: disable the camera-dependent "tighter shadow caster culling" at runtime (stock reads
// rendering/lights_and_shadows/tighter_shadow_caster_culling only at startup).
inline bool shadow_debug_no_caster_culling() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/debug_disable_caster_culling");
}

// Directional shadow sampling (Forward+ scene shader, docs/decisions/0005-directional-shadow-sampling.md):
// skip the shadow lookup of pixels whose N.L makes the light's contribution exactly zero (image-identical).
inline bool shadow_skip_backfacing() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/skip_backfacing_directional");
}
// Omni/spot lights: skip the whole light (shadow lookup, projector, BRDF) where N.L makes its contribution exactly
// zero (image-identical).
inline bool shadow_skip_backfacing_positional() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/skip_backfacing_positional");
}
// PCF path with blend splits: sample the second cascade only where its blend weight is non-zero (image-identical).
inline bool shadow_skip_unblended_cascade() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/skip_unblended_cascade");
}
// PCSS path with blend splits: blend cascades over the last 10 % of each split (like the PCF path) instead of over
// the whole split, sampling the second cascade only there. Changes the look (sharper: higher-resolution cascade).
inline bool shadow_pcss_blend_band() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/pcss_blend_band");
}

// Directional PCSS blocker search early-out (exact): a max pyramid of the shadow atlas proves that the search region
// holds no blocker, in which case stock returns 1.0 anyway.
inline bool shadow_directional_max_mip() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/directional_max_mip");
}

// PCSS of directional lights: penumbra radius = light angular size x (receiver - blocker distance), the parallel-light
// formula. Stock uses the point-light formula (scaled by receiver / blocker distance from the cascade's near plane),
// which overestimates every penumbra and grows without bound for blockers pancaked onto the near plane (casters more
// than "pancake size" in front of the cascade), whose shadows then dissolve. Changes the look (sharper penumbrae).
inline bool shadow_pcss_directional_penumbra() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/pcss_directional_penumbra");
}

inline bool geometry_enabled() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/enabled");
}

// Mesh clusters (docs/decisions/0007-mesh-clusters.md): large static triangle surfaces get a k-d tree of triangle
// clusters when they are created, with a copy of their index buffers (base and every LOD) ordered so that each tree
// node is one contiguous index range. The depth pre-pass and the opaque pass draw only the clusters whose bounds
// intersect the view frustum (a triangle entirely outside the frustum covers no pixel: image-identical). Read when a
// mesh surface is created (clusters are built only while this is on) and every frame (off: whole surfaces).
inline bool mesh_clusters() {
	return geometry_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/mesh_clusters");
}
// Surfaces drawn with blending keep their triangle order and are culled with order-preserving clusters (chunks of
// consecutive triangles); off: drawn whole, as stock.
inline bool mesh_clusters_blended() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/cluster_blended_surfaces");
}
// Maximum triangles per cluster (leaf of the k-d tree).
inline int mesh_cluster_triangles() {
	return CLAMP(GLOBAL_GET_CACHED(int, "rendering/fellow/geometry/cluster_triangles"), 64, 1 << 20);
}
// Surfaces with fewer triangles than this are not clustered (instance culling is enough for them).
inline int mesh_cluster_min_surface_triangles() {
	return MAX(256, GLOBAL_GET_CACHED(int, "rendering/fellow/geometry/cluster_min_surface_triangles"));
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
	SafeNumeric<uint64_t> shadow_cache_copies_skipped;
	SafeNumeric<uint64_t> shadow_cache_cascades_uneconomic; // stock because the cache would cost more than it saves
	SafeNumeric<uint64_t> shadow_cache_cascades_unstable; // stock because the cascade's texel size is changing (zoom)
	SafeNumeric<uint64_t> geometry_cluster_surfaces; // surfaces clustered at creation
	SafeNumeric<uint64_t> geometry_cluster_surfaces_active; // surfaces whose index buffers were put in cluster order
	SafeNumeric<uint64_t> geometry_cluster_surfaces_dropped; // surfaces left in stock order (blending or vertex-moving material)
	SafeNumeric<uint64_t> geometry_cluster_blended_after_activation; // clustered surfaces later set up with a blending material
	SafeNumeric<uint64_t> geometry_cluster_build_usec; // CPU time of cluster builds (worker threads)
	SafeNumeric<uint64_t> geometry_cluster_draws; // draw calls issued for clustered surfaces
	SafeNumeric<uint64_t> geometry_cluster_triangles_drawn; // triangles of clustered surfaces drawn
	SafeNumeric<uint64_t> geometry_cluster_triangles_culled; // triangles of clustered surfaces skipped (outside the frustum)
};

Stats &stats();

} // namespace Fellow

class Dictionary;
namespace Fellow {
Dictionary get_stats_dictionary();

} // namespace Fellow
