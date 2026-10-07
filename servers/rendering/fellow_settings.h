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
#include "servers/rendering/fellow_autotune.h"

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

// SSAO gather with each workgroup's depths and normals loaded once into shared memory (bit-identical; stock: two
// gathers and up to five normal loads per pixel). Read every frame.
inline bool ssao_shared_tile() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/ssao_shared_tile");
}

// TAA motion vectors of static geometry from the depth buffer and the camera matrices (a full-screen pass after the
// opaque pass, as stock does for MetalFX), moving geometry in a separate motion pass; the opaque pass then draws its
// static surfaces without the previous-frame vertex transform and the velocity target. Decision 0013.
inline bool taa_camera_motion_vectors() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/taa_camera_motion_vectors");
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

// Extra timestamp markers inside the render-list and cull steps (profiling only: they split stock's regions).
inline bool list_markers() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/profiling/list_markers");
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

// Directional shadow early-outs (exact, decision 0005): a min/max window pyramid of the directional shadow atlas
// proves that a PCSS search / PCF filter region is fully lit (stock would return 1) or fully shadowed (0), in which
// case the taps are skipped. The pyramid is brought up to date over the atlas rects written each frame.
inline bool shadow_directional_max_mip() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/directional_max_mip") && autotune_allows(TUNE_DIRECTIONAL_PYRAMID);
}

// Directional pyramid: only the windows over the part of each cascade the camera can look up are computed from the
// atlas (the bounding box of the camera frustum up to the cascade's split, projected into the cascade, plus a margin);
// the others hold (0, 1), a bound of any content, so no early-out fires there (exact either way, decision 0005).
inline bool shadow_max_mip_visible_region() {
	return shadow_directional_max_mip() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/max_mip_visible_region");
}

// Positional (omni, spot) shadow PCF early-out (exact): a min/max window pyramid of the positional shadow atlas proves
// that the filter region is fully lit or fully shadowed, in which case the taps are skipped (decision 0005).
inline bool shadow_positional_max_mip() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/positional_max_mip") && autotune_allows(TUNE_POSITIONAL_PYRAMID);
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

inline bool lights_enabled() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/lights/enabled");
}

// Light clusters with depth slices (docs/decisions/0008-light-cluster-depth.md): the 32 depth slices of the Forward+
// light/decal/probe clusters are spaced logarithmically between the camera's near and far planes (stock: linear over
// [0, far], 125 m each with the default far of 4000 m), and every element covers the slices of its own conservative
// view-depth range (stock: the range its proxy mesh rasterises to in each tile, which is sampled). A pixel only loops
// over the elements whose range contains its depth; the elements it no longer visits cannot reach it (zero
// attenuation): image-identical except where stock's sampled ranges miss an element that reaches the pixel.
// Perspective cameras with one view only (others: stock). Read every frame.
inline bool light_cluster_depth_slices() {
	return lights_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/lights/cluster_depth_slices");
}

// The cluster pack pass (bitmasks and per-slice index ranges of every tile, after the element proxies are rasterised)
// with one workgroup per tile and one invocation per depth slice instead of one invocation per tile; the same words
// are stored (decision 0014). Read every frame.
inline bool light_cluster_pack_parallel() {
	return lights_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/lights/cluster_pack_parallel");
}

// Omni and spot lights whose attenuation at the pixel is exactly zero (beyond the range, outside the spot cone) return
// before the attenuation's pow(), the shadow and the BRDF (decision 0008). Exact for materials without custom light
// code, rim, backlight, transmittance, clearcoat, toon shading or shadow-to-opacity (the others keep stock's path).
inline bool light_zero_attenuation_skip() {
	return lights_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/lights/zero_attenuation_skip");
}

// Early submit (docs/decisions/0009-early-submit.md): when the frame reaches the blit to the screen, everything recorded
// so far is submitted before the swap chain image is acquired; the blit and the rest of the frame go into a second
// command buffer that waits on the acquisition and presents. Stock submits the whole frame at the end, after the
// acquisition, and the whole submission waits on the acquired image at the colour-output stage, so the GPU cannot
// start a frame's rendering until the presentation engine has released an image. The same commands in the same
// order: image-identical. Vulkan only (the other drivers keep stock's single submission). Read every frame.
inline bool frame_early_submit() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/early_submit");
}

// Async compute (docs/decisions/0015-async-compute.md): the screen-space effects that only need the depth pre-pass
// (SSAO, SSIL, SSR and their depth downsample) are submitted to a second queue of the main family and run while the
// main queue renders the shadows, the light clusters and the volumetric fog; the opaque pass waits for them. Requires
// the setting at start-up (the device creates the second queue); read every frame after that. Same commands, same
// results: image-identical.
// Present thread: the frame's presentation (vkQueuePresentKHR, which on a hybrid laptop crosses to the display GPU) is
// issued by a dedicated thread instead of the render thread, which goes on with the next frame. The frame is submitted
// signalling a semaphore the presentation waits on; the render thread waits for the presentation to have been issued
// before it acquires, resizes or frees a swap chain. Same commands, same images. Vulkan; read every frame.
// Submit thread (with the present thread's machinery): the frame's submissions (vkQueueSubmit, ~0.1 ms each on the RTX
// laptop) are issued by the present thread too: the end of the frame before its presentation, and the async compute
// splits (0015: three more submissions per frame). The thread takes jobs from a FIFO in the order the render thread
// would have issued them. The render thread flushes the pending memory writes before handing a submission over and
// waits for the FIFO to be empty before any submission of its own, fence wait or swap chain operation. Same
// submissions in the same order. Vulkan; read every frame.
// Parallel replay: at most this many secondary command buffers per draw list (12 by default).
inline int frame_replay_max_segments() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/frame/replay_max_segments");
}

bool cpu_hot_workers_gate_open();
inline bool frame_submit_thread() {
	// Only while the frame is CPU-bound (cpu/hot_workers_gate), like the parallel replay: where the GPU is the limit the
	// render thread waits for it anyway, and the thread's submissions cost Sponza 2 % GPU time (m4_f54_gpu_perf).
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/submit_thread") && (!GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/hot_workers_gate") || cpu_hot_workers_gate_open());
}

inline bool frame_present_thread() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/present_thread");
}

// Parallel replay: large draw lists (192+ draws) are replayed into secondary command buffers by worker threads at the
// end of the frame (render graph), each segment starting with the state the instructions before it set; the render
// pass executes them in order. Same commands in the same order: same images. Vulkan; read every frame.
bool cpu_hot_workers_gate_open();
inline bool frame_parallel_replay() {
	// Like the hot workers, only while the frame is CPU-bound (cpu/hot_workers_gate): a render pass made of secondary
	// command buffers costs the GPU a little (m4_f51_gpu_ab, Bistro exterior: +1.6 % GPU time).
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/parallel_replay") && (!GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/hot_workers_gate") || cpu_hot_workers_gate_open());
}

// Frame plan (with the parallel replay): before the frame's primary command buffer is recorded, every draw list worth
// replaying in secondary command buffers is split (the large ones as above, the others with at least
// frame/replay_plan_min_draws draws as one segment) and the worker threads replay all of them at once, while the render
// thread records the rest of the frame (barriers, compute lists, copies); at each such draw list it executes its
// secondary command buffers, running segments not taken yet itself while it waits. Same commands in the same order.
inline bool frame_replay_frame_plan() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/replay_frame_plan");
}
inline int frame_replay_plan_min_draws() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/frame/replay_plan_min_draws");
}

inline bool frame_async_compute() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/async_compute");
}

// CPU work of the render thread (docs/decisions/0016-parallel-render-lists.md). Each toggle produces the same lists,
// keys, instance data and draws as stock (bit-identical images); read every frame.
inline bool cpu_enabled() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/enabled");
}

// Render lists (opaque, shadow passes) filled by worker threads over ranges of the culled instances, joined in order.
inline bool cpu_parallel_list_fill() {
	return cpu_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/parallel_list_fill");
}

// Instance data of the render lists written by worker threads over ranges of the sorted elements.
inline bool cpu_parallel_instance_data() {
	return cpu_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/parallel_instance_data");
}

// Render lists sorted by stock's introsort on records of the two sort keys copied out of the surfaces (same steps,
// same order, ties included) instead of on pointers dereferenced at every comparison.
inline bool cpu_compact_list_sort() {
	return cpu_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/compact_list_sort");
}

// With the compact sort: large lists sorted by worker threads (introsort's independent subranges in parallel; the
// same steps, the same order).
inline bool cpu_parallel_list_sort() {
	return cpu_compact_list_sort() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/parallel_list_sort");
}

// With the parallel sort: partition steps of large ranges spread over the workers too (exact parallel Hoare
// partition, see fellow_sort.h). Off by default: in City Lab (lists of ~10-20 k elements) its two extra parallel loops
// per step cost more than they save (m4_f52_city_ab: the partition steps 0.218 ms with it, 0.178 ms without).
inline bool cpu_parallel_partition() {
	return cpu_parallel_list_sort() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/parallel_partition");
}

// Ranges at least this large have their partition step spread over the workers (with cpu/parallel_partition).
inline int cpu_parallel_partition_min() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/cpu/parallel_partition_min");
}

// Parallel recording (docs/decisions/0018-parallel-recording.md): a large render list's draws (384 or more) are recorded into its draw
// list by worker threads, each into a segment of the list (its own state tracking and instruction buffer, starting
// with nothing bound, from a run start: the elements the serial loop visits); the segments are joined in order. Same
// draws in the same order with the same state (a segment binds again what the previous one left bound). A list that
// needs a pipeline not compiled yet is recorded again serially (pipeline requests and waits stay the render thread's).
inline bool cpu_parallel_recording() {
	return cpu_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/parallel_recording");
}

// Debug only (wrong images): the parallel instance data steps write to system memory instead of the GPU buffer, to
// measure what the writes over PCIe (device-local, host-visible memory) cost.
inline bool cpu_debug_instance_scratch() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/debug_instance_scratch");
}

// Segments per recording thread (the render thread and its helpers): more balance the load between performance and
// efficiency cores; the replay joins consecutive segments again.
inline int cpu_recording_segments_per_thread() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/cpu/recording_segments_per_thread");
}

// Scene cull: the same ranges and per-range results as stock's threaded cull, but processed by the render thread with
// worker tasks helping, instead of a group task the render thread sleeps on until its last thread has run.
inline bool cpu_parallel_cull() {
	return cpu_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/parallel_cull");
}
// With the parallel cull: this many ranges per thread instead of stock's one (smaller ranges balance better; their results
// are merged in order, the same lists).
inline int cpu_cull_ranges_per_thread() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/cpu/cull_ranges_per_thread");
}

// The shadow passes' lists (directional cascades, positional and cube faces) built together after the last pass is
// set up: filled by worker ranges without writing the instances (each element gets the keys and flags of its pass in a
// record), sorted per pass, instance data in parallel. Same lists, keys, order and instance data as stock.
inline bool cpu_batched_shadow_lists() {
	return cpu_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/batched_shadow_lists");
}

// Batched shadow lists write, per element, only the instance data a shadow pass reads (not prev_transform and
// lightmap_uv_scale, which only motion-vector and lit passes read): 112 of 176 bytes. Same images.
inline bool cpu_compact_shadow_instances() {
	return cpu_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/compact_shadow_instances");
}

// Render list loops (fill, instance data, sort gather) prefetch the instances and surfaces a few iterations ahead.
inline bool cpu_prefetch() {
	return cpu_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/prefetch");
}

// Verification: the render lists built with the CPU toggles are compared, after each build, with stock's single-threaded
// results computed again on the side (sort order, instance data, run lengths; counters cpu_verify_*). Slow.
inline bool cpu_verify() {
	return cpu_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/verify");
}

// Worker tasks posted per parallel loop of the render lists, besides the render thread (0: the render thread alone).
inline int cpu_worker_tasks() {
	return MAX(0, GLOBAL_GET_CACHED(int, "rendering/fellow/cpu/worker_tasks"));
}

// Hot worker tasks: the worker tasks of the render thread's parallel loops (cull, render lists) stay for this many
// microseconds after their last chunk, spinning, ready for the next loop; the frame's first loop has them posted ahead
// (when the cull starts). 0: each loop posts its tasks and they leave when it ends (a sleeping worker can take
// hundreds of microseconds to wake). Changes which thread runs a chunk, never what a chunk does.
//
// Only while the frame is CPU-bound (cpu/hot_workers_gate): on a laptop the spinning cores take power the GPU could use
// (m4_f49_city_ab: GPU time +5-7 %), so when the render thread waits for the GPU (frame fences: the GPU is the limit),
// the workers go cold until the waits stop.
bool cpu_hot_workers_gate_open();
inline int cpu_hot_workers_usec() {
	if (!cpu_enabled()) {
		return 0;
	}
	const int usec = MAX(0, GLOBAL_GET_CACHED(int, "rendering/fellow/cpu/hot_workers_usec"));
	if (usec == 0 || (GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/hot_workers_gate") && !cpu_hot_workers_gate_open())) {
		return 0;
	}
	return usec;
}
// How an idle hot worker waits for the next loop: 0 = a pause loop (spinning), 1 = UMWAIT in C0.1, 2 = UMWAIT in C0.2
// (x86 WAITPKG: the core waits in a low-power state until the loop's state is written; CPUs without it spin as in 0).
// Less power for the spinning cores leaves more of a laptop's shared budget to the GPU. Never changes results.
inline int cpu_hot_workers_wait() {
	return CLAMP(GLOBAL_GET_CACHED(int, "rendering/fellow/cpu/hot_workers_wait"), 0, 2);
}
// The main RenderingDevice reports, once per frame, how long the render thread waited for an earlier frame's fence and
// the time since the previous report (gate of the hot workers).
void frame_gpu_wait_report(uint64_t p_wait_usec, uint64_t p_frame_usec);

// Mix-blended materials (alpha pass, no depth or stencil writes, no alpha antialiasing): fragments whose final alpha is
// exactly 0 are discarded before lighting — blending them leaves the colour target unchanged (src * 0 + dst * 1), so
// the image is the same; stock lights and fogs them anyway. Read every frame.
inline bool blended_alpha_zero_skip() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shading/blended_alpha_zero_skip");
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
	SafeNumeric<uint64_t> frame_early_submits; // frames whose rendering was submitted before the swap chain acquisition
	SafeNumeric<uint64_t> frame_async_uses; // async compute segments (decision 0015)
	SafeNumeric<uint64_t> frame_async_conflicts; // of which the concurrent part had to wait (shared resource)
	SafeNumeric<uint64_t> shadow_pyramid_builds; // min/max pyramid updates that wrote something (directional and positional)
	SafeNumeric<uint64_t> shadow_pyramid_texels; // finest-level pyramid texels written (each covers 4x4 new atlas texels)
	SafeNumeric<uint64_t> shadow_pyramid_texels_needed; // of those, the ones computed from the atlas (the rest set to (0, 1))
	SafeNumeric<uint64_t> cpu_shadow_batches; // batched shadow list builds (decision 0016)
	SafeNumeric<uint64_t> cpu_shadow_batch_passes; // shadow passes built in them
	SafeNumeric<uint64_t> cpu_shadow_batch_fallbacks; // of the builds, built stock's way (instance buffer replaced mid-way)
	SafeNumeric<uint64_t> cpu_verify_checks; // lists or passes compared with stock's results (cpu/verify)
	SafeNumeric<uint64_t> cpu_verify_mismatches; // elements or values that differed
	SafeNumeric<uint64_t> cpu_loops; // parallel loops of the render thread (FellowParallelFor) with 2 or more chunks
	SafeNumeric<uint64_t> cpu_loop_chunks; // their chunks
	SafeNumeric<uint64_t> cpu_helper_chunks; // of those, chunks run by worker tasks (the others: by the render thread)
	SafeNumeric<uint64_t> rd_frames; // RenderingDevice frames ended (main device)
	SafeNumeric<uint64_t> rd_graph_end_usec; // CPU time of the render graph's replay into the command buffer (_end_frame)
	SafeNumeric<uint64_t> rd_execute_usec; // CPU time of the frame's submission and presentation (_execute_frame)
	SafeNumeric<uint64_t> rd_fence_wait_usec; // time the render thread waited for frame fences (GPU behind the CPU)
	SafeNumeric<uint64_t> rd_vk_submit_usec; // Vulkan: time in vkQueueSubmit (frame submissions)
	SafeNumeric<uint64_t> rd_vk_present_usec; // Vulkan: time in vkQueuePresentKHR
	SafeNumeric<uint64_t> rd_vk_presents; // Vulkan: presentations
	SafeNumeric<uint64_t> frame_present_thread_uses; // presentations handed to the present thread
	SafeNumeric<uint64_t> frame_present_thread_wait_usec; // time the render thread waited for the present thread
	SafeNumeric<uint64_t> frame_replay_lists; // draw lists replayed by worker threads (parallel replay)
	SafeNumeric<uint64_t> frame_replay_segments; // their segments (secondary command buffers)
	SafeNumeric<uint64_t> rd_graph_commands; // render graph commands replayed
	SafeNumeric<uint64_t> rd_graph_sort_nsec; // the graph's dependency sort before the replay
	SafeNumeric<uint64_t> rd_graph_barriers_nsec; // barriers grouped and recorded
	SafeNumeric<uint64_t> rd_graph_draw_nsec; // draw lists replayed (vkCmd* calls)
	SafeNumeric<uint64_t> rd_graph_draw_lists; // draw lists replayed
	SafeNumeric<uint64_t> rd_graph_draw_bytes; // their instruction bytes
	SafeNumeric<uint64_t> cpu_hot_gate_closed_frames; // frames during which the hot workers' gate was closed (GPU-bound)
	SafeNumeric<uint64_t> frame_submit_jobs; // jobs (submissions, presentations) handed to the present/submit thread
	SafeNumeric<uint64_t> rd_async_end_usec; // CPU time of the render graph's replay at the async compute splits
	SafeNumeric<uint64_t> rd_async_submit_usec; // CPU time of the async compute splits' submissions (or hand-overs)
	SafeNumeric<uint64_t> cpu_record_lists; // render lists recorded by worker threads (parallel recording)
	SafeNumeric<uint64_t> cpu_record_segments; // their segments
	SafeNumeric<uint64_t> cpu_record_fallbacks; // lists recorded again serially (a pipeline not compiled yet)
	SafeNumeric<uint64_t> frame_replay_recorded_lists; // of the lists replayed in parallel, split where they were recorded
	SafeNumeric<uint64_t> frame_plan_lists; // draw lists replayed from the frame plan (frame/replay_frame_plan)
	SafeNumeric<uint64_t> frame_plan_segments; // their segments
	SafeNumeric<uint64_t> frame_plan_wait_nsec; // time the render thread spent at those lists until their segments were done
	SafeNumeric<uint64_t> autotune_evals; // autotune evaluations finished (decision 0020)
	SafeNumeric<uint64_t> autotune_flips; // evaluations that changed a choice
	SafeNumeric<uint64_t> autotune_off_frames; // frames x tunables that ran with the optimization off
	SafeNumeric<uint64_t> cpu_hot_waits; // low-power waits of idle hot workers (cpu/hot_workers_wait, CPUs with WAITPKG)
};

Stats &stats();

} // namespace Fellow

class Dictionary;
namespace Fellow {
Dictionary get_stats_dictionary();

} // namespace Fellow
