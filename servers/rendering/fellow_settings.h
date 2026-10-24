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
// Glow's Gaussian blur (every level of the chain): 0 stock; 1 its shared-memory layout padded so that the vertical pass
// is free of bank conflicts; 2 in 16 x 16 tiles instead of 8 x 8 (2.25 bilinear samples per output instead of 4), also
// without conflicts. The same samples, summed in the same order (decision 0052). Read every frame.
inline int glow_blur() {
	return postfx_enabled() ? CLAMP(GLOBAL_GET_CACHED(int, "rendering/fellow/postfx/glow_blur"), 0, 2) : 0;
}

// SSR's roughness filter (a blur per mip of the reflection) with its shared-memory arrays padded so that neither pass
// has bank conflicts; the same values, the same sums (decision 0052). Read every frame.
inline bool ssr_filter_padded() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/ssr_filter_padded");
}

// Volumetric fog's density pass without its area light loop when the light cluster holds no area light, and without
// its VoxelGI and directional loops when none is injected or the frame has none (specialization constants; the loops
// would run no iteration; decision 0056, on by default). Read every frame.
inline bool fog_specialized() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/fog_specialized");
}
// Forward+ tone mapping with the push constants' decisions (flags, tone mapper, glow mode, glow levels, glow map) also
// as specialization constants: the driver compiles only the paths taken; the same arithmetic (decision 0055, on by
// default). Read every frame.
inline bool tonemap_specialized() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/tonemap_specialized");
}

// Volumetric fog's density pass (decision 0053): 0 stock; 1 its omni/spot/area light loops walked by the subgroup over
// the union of its froxels' lights (each froxel takes only its own, in stock's order); 2 the same with workgroups of 2 x 2
// x 16 froxels (a warp's froxels about one cluster tile); 3 those workgroups with stock's loops. Read every frame.
inline int fog_light_loop() {
	return postfx_enabled() ? CLAMP(GLOBAL_GET_CACHED(int, "rendering/fellow/postfx/fog_light_loop"), 0, 3) : 0;
}

inline bool ssao_shared_tile() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/ssao_shared_tile");
}

// SSAO and SSIL gathers with each workgroup computing the pixels of one sample rotation in a 40 x 8 tile, so that its
// threads sample at nearly the same offsets from their pixels (docs/decisions/0024-coherent-gather.md; every pixel is
// computed as stock computes it). Read every frame.
// SSAO/SSIL gathers: this many taps' fetches issued together, then used in stock's order with stock's arithmetic (exact;
// docs/decisions/0041-ss-tap-batch.md). Start-up; 0 or 1: stock's loop.
inline int ss_tap_batch() {
	return postfx_enabled() ? CLAMP(int(GLOBAL_GET("rendering/fellow/postfx/ss_tap_batch")), 0, 8) : 0;
}
inline bool ss_coherent_gather() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/ss_coherent_gather");
}

// Temporal SSAO (docs/decisions/0028-temporal-ssao.md; visually lossless, not bit-exact): the gather takes every other
// tap, alternating halves between frames, and adds the other half from the previous frame where the same surface was
// seen at the reprojected pixel (a still view sums every tap of the stock gather). High quality and below, full size.
// Read every frame.
inline bool ssao_temporal() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/ssao_temporal");
}

// SSIL gather with alternating halves of its taps, the other half from the previous frame (as ssao_temporal; LOW to
// HIGH quality, full size, one view; docs/decisions/0030-temporal-ssil.md). Not exact: measured. Read every frame.
inline bool ssil_temporal() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/ssil_temporal");
}

// Volumetric fog with half its froxels lit per frame (checkerboard, alternating), the others reprojected; only with
// the environment's temporal reprojection (docs/decisions/0031-fog-half-rate.md). Not exact: measured. Read every frame.
inline bool fog_half_rate() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/fog_half_rate");
}

// The auto exposure's luminance reduction with two barriers per workgroup instead of seven (the same additions in the
// same order; docs/decisions/0025-luminance-reduce.md). Read every frame.
inline bool fast_luminance_reduce() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/fast_luminance_reduce");
}

// SSAO/SSIL interleave passes bounded by the output image's size (decision 0067).
inline bool ss_interleave_exact_bounds() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/ss_interleave_exact_bounds");
}

// SSAO/SSIL depth mipmaps without stock's racing stores (decision 0067).
inline bool ss_depth_mips_exact() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/ss_depth_mips_exact");
}
// postfx/ssr_resolve_shared (decision 0074): SSR's resolve reads and prepares each half-resolution sample once per
// workgroup (shared memory) instead of once per pixel that uses it; the same reads and expressions. Read every frame.
inline bool ssr_resolve_shared() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/ssr_resolve_shared");
}
// Debug (docs/decisions/0026-occlusion-culling.md): after the depth pre-pass, count the opaque list elements a depth
// pyramid of this frame's pre-pass would hide (measurement only; a GPU sync per frame).
inline bool debug_occlusion_stats() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/debug/occlusion_stats");
}
// MultiMesh culling's instance counters (tested, drawn, coarse) read back from the GPU every frame (decision 0034).
inline bool debug_multimesh_stats() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/debug/multimesh_stats");
}
// Debug, wrong images: opaque list elements the test found hidden are left out in the following frames (to size what
// culling would save; decision 0026).
inline bool debug_occlusion_cull_approx() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/debug/occlusion_cull_approx");
}

// Debug: both reductions every frame on the same inputs, every level read back and compared (slow: a GPU sync per frame).
inline bool verify_luminance_reduce() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/verify_luminance_reduce");
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
// TAA resolve in 16 x 16 workgroups, every thread loading the shared tile (exact: the same neighbourhood values per pixel).
// SSIL's copy of the last frame: its full-size level skipped where nothing reads it, level 1 made from the internal
// texture directly (exact; runtime; decision 0044).
inline bool ss_last_frame_direct() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/ss_last_frame_direct");
}
inline bool taa_shared_velocity() {
	return postfx_enabled() && bool(GLOBAL_GET("rendering/fellow/postfx/taa_shared_velocity"));
}
inline bool taa_tile16() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/taa_tile16");
}
// postfx/taa_strided_load (decision 0075): TAA's 8 x 8 workgroups load their 10 x 10 tile with all 64 invocations (at
// most two texels each) instead of four texels each by 25 of them. The same texels, the same expressions per texel.
// animation/elide_modifier_backup (decision 0076): a Skeleton3D whose modifiers all change nothing this frame (inactive
// with full influence; the compatibility PhysicalBoneSimulator3D every Skeleton3D has in builds with the deprecated API,
// in a skeleton without physical bones) does not save and restore its bones around them.
inline bool animation_elide_modifier_backup() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/animation/elide_modifier_backup");
}
// animation/full_weight_slerp (decision 0076): Animation::interpolate_via_rest() at weight 1 (a track of the only animation
// playing) computes Quaternion::slerp()'s result without its acos() and sin() calls: the same values bit for bit.
inline bool animation_full_weight_slerp() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/animation/full_weight_slerp");
}
// animation/track_shortcuts (decision 0076): an animation track's interpolation without the binary search for its last key
// when only the last can be at the animation's end, and rotation and scale keys not passed through
// _post_process_key_value() when no script overrides it (it returns them unchanged).
inline bool animation_track_shortcuts() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/animation/track_shortcuts");
}
// animation/batched_skin_transforms (decision 0077): a Skeleton3D hands each skin's bone transforms to the RenderingServer in
// one call instead of one call per bone (each with the server's wrapper and a mutex-guarded lookup of the skeleton).
inline bool animation_batched_skin_transforms() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/animation/batched_skin_transforms");
}
// animation/key_hints (decision 0076): each track's key found in the previous frame (then the next) tried before the
// binary search, used only when it and the next key enclose the time strictly, outside the search's tolerance.
inline bool animation_key_hints() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/animation/key_hints");
}
// post/motion_blur/* (decision 0078): motion blur from the velocity buffer after TAA (a visual feature, off by default).
inline bool motion_blur_enabled() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/post/motion_blur/enabled");
}
inline float motion_blur_amount() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/motion_blur/amount");
}
inline int motion_blur_samples() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/post/motion_blur/samples");
}
inline float motion_blur_max_radius() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/motion_blur/max_radius");
}
// post/lens/* (decision 0079): lens effects in the tone mapping pass (visual features, 0 = off).
inline float lens_chromatic_aberration() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/lens/chromatic_aberration");
}
inline float lens_vignette() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/lens/vignette");
}
inline float lens_vignette_smoothness() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/lens/vignette_smoothness");
}
inline float lens_film_grain() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/post/lens/film_grain");
}
// raytracing/* (decision 0080, start-up): ray tracing with ray queries. raytracing_active(): requested and the device
// supports ray queries (decided on first call, once the rendering device exists).
inline bool raytracing_requested() {
	return enabled() && bool(GLOBAL_GET("rendering/fellow/raytracing/enabled"));
}
bool raytracing_active();
inline float raytracing_radius() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/raytracing/radius");
}
inline bool raytracing_skinned() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/raytracing/skinned");
}
// debug/raytracing_view: RTAO's buffer shows the ray tracing scene from the camera instead (1: hit distances, 2: where it
// matches the depth buffer); with the viewport's SSAO debug draw.
inline int raytracing_debug_view() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/debug/raytracing_view");
}
inline bool raytracing_decode_positions() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/debug/raytracing_decode_positions");
}
// raytracing/ambient_occlusion (decision 0081): where the environment enables SSAO, ray-traced ambient occlusion
// writes the buffer SSAO would.
inline bool raytracing_ao() {
	return raytracing_active() && GLOBAL_GET_CACHED(bool, "rendering/fellow/raytracing/ambient_occlusion");
}
inline int raytracing_ao_rays() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/raytracing/ao_rays");
}
inline bool raytracing_ao_interleaved() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/raytracing/ao_interleaved");
}
// raytracing/sun_shadows (decision 0082): the first directional light's shadow from rays, read by the opaque colour
// pass in place of the shadow map within half the ray tracing radius of the camera.
inline bool raytracing_sun_shadows() {
	return raytracing_active() && GLOBAL_GET_CACHED(bool, "rendering/fellow/raytracing/sun_shadows");
}
inline int raytracing_sun_shadow_rays() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/raytracing/sun_shadow_rays");
}
// raytracing/gi (decision 0083): a volume of irradiance probes around the camera, traced every frame, read by the colour
// passes in place of the environment's ambient light.
inline bool raytracing_gi() {
	return raytracing_active() && GLOBAL_GET_CACHED(bool, "rendering/fellow/raytracing/gi");
}
inline float raytracing_gi_probe_spacing() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/raytracing/gi_probe_spacing");
}
inline int raytracing_gi_probes_xz() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/raytracing/gi_probes_xz");
}
inline int raytracing_gi_probes_y() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/raytracing/gi_probes_y");
}
inline int raytracing_gi_rays() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/raytracing/gi_rays");
}
inline float raytracing_gi_hysteresis() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/raytracing/gi_hysteresis");
}
inline float raytracing_gi_energy() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/raytracing/gi_energy");
}
inline int raytracing_gi_dump_every() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/debug/raytracing_gi_dump_every");
}
inline bool raytracing_gi_in_shader() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/debug/raytracing_gi_in_shader");
}
inline int raytracing_gi_update_interval() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/raytracing/gi_update_interval");
}
// raytracing/reflections (decision 0084): where the environment enables SSR, a traced reflection per pixel up to
// reflections_max_roughness in place of SSR (and of the probes' and the sky's specular light there).
inline bool raytracing_reflections() {
	return raytracing_active() && GLOBAL_GET_CACHED(bool, "rendering/fellow/raytracing/reflections");
}
inline float raytracing_reflections_max_roughness() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/raytracing/reflections_max_roughness");
}
inline bool raytracing_reflections_interleaved() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/raytracing/reflections_interleaved");
}
// Whether the frame's renderer needs the ray tracing scene (any ray-traced effect on).
inline bool raytracing_scene_wanted() {
	return raytracing_ao() || raytracing_sun_shadows() || raytracing_gi() || raytracing_reflections();
}
inline bool taa_strided_load() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/taa_strided_load");
}
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

// TAA direct output: with the history ping-pong, the post-processing chain (auto exposure, glow, tone mapping) reads the
// resolve's output texture (this frame's history) instead of a copy of it in the internal colour texture: one
// full-resolution copy less per frame, the same texels read. Single view, no depth of field and no debug draw mode
// (those modify or show the internal texture: the copy is made then).
inline bool taa_direct_output() {
	return postfx_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/postfx/taa_direct_output");
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
// shadows/atlas_simulated_time (decision 0071): the positional shadow atlas moves a light to a quadrant of another
// size only 500 ms after its last allocation (stock's shadow_atlas_realloc_tolerance_msec), measured on the wall clock.
// When the main loop simulates a fixed frame rate (--fixed-fps, Movie Maker mode) the frames are not drawn at that
// rate, so which frame reallocates depended on how fast the machine drew the frames before it; with this setting the
// tolerance counts the simulated time (the renderer's frames at the fixed rate). At a free frame rate: the wall clock,
// as stock.
uint64_t shadow_atlas_tick_msec();
// frame/movie_wait_pipelines (decision 0072): while Movie Maker mode writes a video (--write-movie), a 3D draw whose
// specialized pipeline is not compiled yet waits for it instead of drawing with the ubershader, as the 2D renderer
// always does. The recorded frames are drawn by the same pipelines on every run. Read every frame.
bool frame_movie_wait_pipelines();

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
// Directional PCSS decided from the min/max pyramid over the penumbra-scaled filter radius after the blocker search
// (docs/decisions/0039-pcss-filter-early-out.md); needs shadows/directional_max_mip.
inline bool shadow_pcss_filter_early_out() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/pcss_filter_early_out");
}
// Soft shadow filter taps (PCF, PCSS blocker search and filter; directional and positional) fetched a few at a time
// and their results used afterwards in the same order: the same arithmetic, the fetches in flight together
// (docs/decisions/0040-batched-shadow-taps.md). Specialization constant, runtime.
inline bool shadow_batched_taps() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/batched_taps");
}
// Directional PCSS blocker search shared by each 2 x 2 pixel quad: each pixel takes a quarter of the search taps and the
// quad sums them (visually lossless, changes the image; docs/decisions/0042-pcss-quad-search.md). Specialization
// constant, runtime; needs subgroup quad operations in fragment shaders.
// Omni/spot lights: the fields of a light's tests and shading read together at the start of its function instead of
// each where it is used (the same values; docs/decisions/0045-light-fields-early.md). Specialization constant, runtime;
// used in frames with omni or spot lights in view (in a scene without them it only changes the shader's code).
// Omni/spot lights: after the depth pre-pass, per 8 x 8 pixel tile, the lights that can reach its geometry; the opaque
// colour pass's light loops visit only those (exact: the others fail the range test of every fragment there;
// docs/decisions/0049-light-tile-cull.md). Runtime; with lights/zero_attenuation_skip, without MSAA, multiview or VRS.
// The largest pipeline cache file loaded at start-up, in bytes (decision 0050).
inline uint64_t pipeline_cache_max_bytes() {
	return uint64_t(CLAMP(int(GLOBAL_GET("rendering/fellow/frame/pipeline_cache_max_mb")), 64, 2000)) * 1024 * 1024;
}
inline bool light_tile_cull_setting() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/lights/enabled") && GLOBAL_GET_CACHED(bool, "rendering/fellow/lights/tile_cull");
}
// The setting, as autotune allows it in this frame (it measures the pass and its use against neither, per scene).
inline bool light_tile_cull() {
	return light_tile_cull_setting() && autotune_allows(TUNE_LIGHT_TILE_CULL);
}
inline bool light_tile_cull_passthrough() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/debug/tile_cull_passthrough");
}
// Light tile culling v6 (decision 0049): the pass split around the async compute part (its depth bounds before it, the
// rest beside it). 0: when async compute runs, 1: always (also without async compute: tests), 2: never. Read every frame.
inline int light_tile_cull_split() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/debug/tile_cull_split");
}

// Render graph: 1, a barrier for a resource written in an earlier graph (command buffer) of the same frame (see
// RenderingDeviceGraph::fellow_frame_first_graph); 2, also a full barrier at the start of every main queue command
// buffer after the first of the frame (diagnostics); 0, neither. Read every frame.
inline int segment_barriers() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/debug/segment_barriers");
}

// Render graph (decision 0062): a read with the usage a resource already has, from a pipeline stage the barrier after
// the graph's last write did not cover, gets a memory barrier (stock's usage tracking assumes the earlier barrier
// made the write visible to every later reader of that usage). Off with FellowEngine disabled (stock).
inline bool graph_visibility_barriers() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/debug/visibility_barriers");
}

// The colour passes' pipelines specialized for what the frame does not have (bits): 1 no decal, 2 no reflection probe,
// 8 no omni light, 16 no spot light in the light cluster, 4 no directional light, 32 no shadowed omni or spot light
// (their code is not compiled; it would not run; the values it could have changed pass fellow_keep; decision 0057, all
// six on by default). Read every frame.
inline int absent_features_specialized() {
	return enabled() ? (GLOBAL_GET_CACHED(int, "rendering/fellow/shading/absent_features_specialized") & 63) : 0;
}

inline bool light_tile_cull_pass_only() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/debug/tile_cull_pass_only");
}
inline bool light_fields_early() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/lights/enabled") && GLOBAL_GET_CACHED(bool, "rendering/fellow/lights/fields_early");
}
// Omni/spot lights: the next light's layer mask, bake mode and range test fields (1, 3), or all its hot fields (2, 4),
// read while the current light is processed; 3 and 4 also read the next cluster word's mask (and its first light) while
// the current word's lights are processed (with lights/fields_early; the same values;
// docs/decisions/0046-light-prefetch-next.md). Specialization constants, runtime.
// Omni/spot lights: the fields the light loop reads for every light it visits, read from a compact copy (80 bytes per
// light) in a storage buffer (1) or a uniform buffer (2) instead of the 240-byte LightData (with lights/fields_early;
// the same values; docs/decisions/0047-light-hot-fields-buffer.md). Specialization constants, runtime.
inline int light_hot_fields_buffer() {
	return light_fields_early() ? CLAMP(GLOBAL_GET_CACHED(int, "rendering/fellow/lights/hot_fields_buffer"), 0, 2) : 0;
}
inline int light_prefetch_next() {
	return light_fields_early() ? CLAMP(GLOBAL_GET_CACHED(int, "rendering/fellow/lights/prefetch_next"), 0, 4) : 0;
}
inline bool shadow_pcss_quad_search() {
	return shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/pcss_quad_search");
}
// Diagnostics: which kinds of lights' filters batch their taps (1 directional, 2 positional, 3 both).
inline bool shadow_batched_taps_directional() {
	return shadow_batched_taps() && (int(GLOBAL_GET_CACHED(int, "rendering/fellow/debug/batched_taps_kinds")) & 1);
}
inline bool shadow_batched_taps_positional() {
	return shadow_batched_taps() && (int(GLOBAL_GET_CACHED(int, "rendering/fellow/debug/batched_taps_kinds")) & 2);
}
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
// Occlusion culling of the opaque pass (docs/decisions/0026-occlusion-culling.md): after the full depth pre-pass, the
// elements whose surface's bounding box lies behind the depth buffer everywhere it covers are left out of the opaque
// pass by the GPU (per run of instanced elements, the visible ones compacted in order and drawn indirectly). Exact: such
// elements have no fragment that passes the opaque pass's depth test. Needs persistent instance data; read every frame.
inline bool occlusion_culling() {
	return geometry_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/occlusion_culling");
}
// MultiMesh instance culling (docs/decisions/0034-multimesh-culling.md): the instances of a MultiMeshInstance3D are tested
// one by one against each pass's view volume (camera, every shadow pass) on the GPU, and only the visible ones are drawn
// (compacted in order, indirect draws). Exact for materials that do not move vertices or read INSTANCE_ID (others are
// drawn whole); MultiMeshes with at least geometry/multimesh_cull_min_instances instances. Read every frame.
inline bool multimesh_culling() {
	return geometry_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/multimesh_culling");
}
// Per-instance LOD for culled MultiMeshes (docs/decisions/0035-multimesh-instance-lod.md): in the camera's passes each
// instance gets the detail level stock's mesh LOD rule gives a node of its own (instead of the level of the MultiMesh's
// whole box), as if each instance were a node of its own. Changes the image (coarser far instances, at the error the
// project's mesh LOD threshold allows); off by default.
inline bool multimesh_instance_lod() {
	return geometry_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/multimesh_instance_lod");
}
// MultiMesh culling of materials that move vertices (docs/decisions/0036-multimesh-cull-vertex-motion.md): on nodes
// with an extra cull margin, the margin is taken as the bound of the vertices' displacement (in the node's space) and
// added to each instance's box. Exact when the material keeps within it (what stock assumes of the whole node); off.
inline bool multimesh_cull_vertex_motion() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/multimesh_cull_vertex_motion");
}
// A pass group's (camera or shadows) culling runs only when its MultiMesh draws, with every instance, would draw at
// least this many indices: below it the dispatches cost about what they could save (decision 0034).
inline int multimesh_cull_min_indices() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/geometry/multimesh_cull_min_indices");
}
inline int multimesh_cull_min_instances() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/geometry/multimesh_cull_min_instances");
}
// The depth pre-pass draws the elements with the shared depth material first, then the alpha-tested and vertex-moving
// ones (docs/decisions/0029-prepass-order.md): the same depth buffer; read every frame.
inline bool prepass_simple_first() {
	return geometry_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/prepass_simple_first");
}
// The camera passes draw a large surface's clusters in front-to-back order (decision 0058): nodes entirely in view are
// split further while they hold more than this many triangles; 0, stock's (the tree's) order. Read every frame.
inline int cluster_front_to_back() {
	return geometry_enabled() ? MAX(0, GLOBAL_GET_CACHED(int, "rendering/fellow/geometry/cluster_front_to_back")) : 0;
}

// Large static surfaces' clusters each at their own mesh LOD: the level stock would choose for the cluster if it were an
// instance of its own (Godot's screen-space edge length against the LOD threshold, from the main camera's distance to
// it), never finer than the instance's level. Changes the image (as LOD does): off until measured (decision 0063).
inline bool cluster_lod() {
	return mesh_clusters() && GLOBAL_GET_CACHED(bool, "rendering/fellow/geometry/cluster_lod");
}
// frame/depth_pipeline_keys (decision 0069): the depth passes' pipeline keys without the colour passes' FellowEngine
// bits, which their code does not read.
inline bool depth_pipeline_keys() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/depth_pipeline_keys");
}
// frame/clear_new_textures (decision 0070): a texture created without data that a pass can write (storage, colour or
// depth attachment) is cleared to zero before its first use, as stock does on Direct3D 12. A read of texels no pass
// wrote reads zero, not what the memory held before. Read when a texture is created.
inline bool clear_new_textures() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/clear_new_textures");
}
// A draw whose specialized pipeline is still compiling uses an already compiled pipeline that differs only in
// FellowEngine's per-frame specialization bits whose other value gives the same image (0057's bits clear, light tile
// culling or the shadow pyramids off, the light fields early either way) instead of stock's ubershader (decision 0059).
// Read every frame.
inline bool pipeline_fallback() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/pipeline_fallback");
}
// With async compute, SSIL (and SSR) run on the main queue beside the second queue's SSAO (the depth preparation before
// both), instead of after SSAO on the second queue (decision 0060). Read every frame.
// 0: stock's layout (every effect on the second queue); 1: SSIL on the main queue; 2: SSIL and SSR on the main queue;
// 3: SSIL on the main queue, SSR too where autotune measures it faster (TUNE_SSR_MAIN). The mode for this frame.
inline int frame_async_ss_split() {
	if (!enabled()) {
		return 0;
	}
	const int mode = GLOBAL_GET_CACHED(int, "rendering/fellow/frame/async_ss_split");
	return mode == 3 ? (autotune_allows(TUNE_SSR_MAIN) ? 2 : 1) : mode;
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

// The cluster pack pass with one invocation per element (64 at a time) instead of a walk of the tile's elements, and the
// cluster render buffer (21 MB at 1080p with 512 elements per type) left cleared by it instead of cleared before every
// build; the same words are stored (decision 0051). Read every frame.
inline bool light_cluster_pack_elements() {
	return lights_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/lights/cluster_pack_elements");
}

// The cluster build's render pass draws consecutive elements with the same proxy mesh (and a conservative depth range)
// as one instanced draw instead of one draw per element; the same tiles are marked (decision 0051). Read every frame.
inline bool light_cluster_instanced_draws() {
	return lights_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/lights/cluster_instanced_draws");
}

// Diagnostics (decision 0051): with cluster_pack_elements and cluster_instanced_draws, the render pass still sets the
// depth words of elements whose slices come from their range (v1 of the decision). Read every frame.
inline bool light_cluster_keep_depth_words() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/debug/cluster_keep_depth_words");
}

// Debug (decision 0051): every light cluster build with cluster_pack_elements or cluster_instanced_draws is repeated with
// one draw per element and the pack of decision 0014 (or stock's), and both results are read back and compared word for
// word (stats lights_cluster_verify_*); after the element pack, the render buffer is checked to be all zero. Slow.
// 2: the same with a deliberate error in the element pack (bit 31 of every word dropped), the negative control.
inline int light_cluster_verify() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/debug/cluster_verify");
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

// Shaders read from the shader cache: a version's variants are created on worker threads at once instead of one after
// the other on the thread that needs them, and the driver's part of creating a shader (decoding the SPIR-V, the shader
// modules, the layouts) runs outside the device's lock where the driver allows it (Vulkan). The same shaders; the first
// frame of a scene waited about a second for them (decision 0061).
inline bool frame_parallel_shader_create() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/parallel_shader_create");
}

// Shader variants read from the shader cache are created when first asked for instead of all at once: a colour material
// has 22 variants (depth, shadow, normal-roughness, voxel GI, SDF, material passes; colour passes with and without
// separate specular, lightmap, motion vectors, ubershader), of which a frame uses a handful (decision 0061).
inline bool frame_lazy_shader_variants() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/lazy_shader_variants");
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
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/replay_frame_plan") && autotune_allows(TUNE_FRAME_PLAN);
}
inline int frame_replay_plan_min_draws() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/frame/replay_plan_min_draws");
}

inline bool frame_async_compute() {
	return enabled() && GLOBAL_GET_CACHED(int, "rendering/fellow/frame/async_compute") != 0 && autotune_allows(TUNE_ASYNC_COMPUTE);
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

// Where the per-frame instance data buffers of the forward renderer live (docs/decisions/0021-instance-buffer-memory.md):
// 0 stock (device-local memory the CPU writes over PCIe where the GPU has it host-visible), 1 system memory
// (write-combined; the GPU reads it over PCIe), 2 system memory, cached. Same data either way; the buffers are created
// again when it changes.
inline int cpu_instance_buffer_memory() {
	return cpu_enabled() ? CLAMP(GLOBAL_GET_CACHED(int, "rendering/fellow/cpu/instance_buffer_memory"), 0, 2) : 0;
}

// Persistent instance data (docs/decisions/0022-persistent-instance-data.md): each surface's instance data is kept in a
// buffer from frame to frame and rewritten only when it changes; per frame each list element is 16 bytes (the surface's
// entry, the flags and GI offset of its pass) instead of the whole 176. Runtime; the frame's choice is taken when its
// first list is filled. A tunable of autotune (it moves work from the render thread to the GPU: the shaders read each
// element's entry through its slot).
inline bool cpu_persistent_instances() {
	return cpu_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/persistent_instances") && autotune_allows(TUNE_PERSISTENT_INSTANCES);
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
// -1 (the default, decision 0073): half the logical processors less two, at least 1 and at most 6. Ten had left the
// benchmark machine (8 performance cores) without a free core for the system's and the driver's threads that the
// presentation needs: City Lab's frames of 6-50 ms.
int cpu_worker_tasks_auto();
inline int cpu_worker_tasks() {
	const int n = GLOBAL_GET_CACHED(int, "rendering/fellow/cpu/worker_tasks");
	return n < 0 ? cpu_worker_tasks_auto() : n;
}

// Hot worker tasks: the worker tasks of the render thread's parallel loops (cull, render lists) stay for this many
// microseconds after their last chunk, spinning, ready for the next loop; the frame's first loop has them posted ahead
// (when the cull starts). 30 by default (decision 0065: with 300 they spun through a whole CPU-bound frame and the
// presentation waited a scheduler quantum now and then; the loops of a burst still follow each other within 30 µs). 0: each loop posts its tasks and they leave when it ends (a sleeping worker can take
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
// Experiment (decision 0065, off): an idle hot worker gives its core to any other thread ready to run on it
// (SwitchToThread / sched_yield) every 64 pauses (about every 2-5 µs). Meant for City Lab's frames of 16-45 ms (each one
// vkQueuePresentKHR call waiting while the hot workers spin through a CPU-bound frame); it left them (m13_f189) and cost
// 7.9 % of the frame; the shorter linger (cpu/hot_workers_usec 30) removes them. Never changes results.
inline bool cpu_hot_workers_yield() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/hot_workers_yield");
}
// An idle hot worker spins at the lowest thread priority and returns to normal before it claims a chunk (decision 0065;
// Windows): any thread of normal priority that becomes ready preempts a spinning worker on whichever core it spins.
inline bool cpu_hot_workers_idle_priority() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/cpu/hot_workers_idle_priority");
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

// Adaptive shading rate (docs/decisions/0027-adaptive-shading-rate.md; visually lossless, not bit-exact): the 3D
// viewports get a shading rate attachment, set every frame from the previous frame's final image: tiles whose content
// varies less than adaptive_vrs_threshold (luma, 0-1) at half or quarter resolution are shaded coarsely in the opaque and
// transparent passes. Materials that discard or write depth keep full rate. The attachment is created with the render
// buffers (read then); the threshold is read every frame (0: every tile at full rate).
inline bool adaptive_vrs() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shading/adaptive_vrs");
}
inline float adaptive_vrs_threshold() {
	// 0 (every tile at full rate, the estimate skipped) while autotune runs the frame without it (decision 0020).
	return autotune_allows(TUNE_ADAPTIVE_VRS) ? GLOBAL_GET_CACHED(float, "rendering/fellow/shading/adaptive_vrs_threshold") : 0.0f;
}
// Coarsest rate: 0 = 2 x 2, 1 = 4 x 4.
inline int adaptive_vrs_max_rate() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/shading/adaptive_vrs_max_rate");
}
// Each tile is shaded at full rate one frame in this many (0: never), so that detail lost to coarse shading is seen again.
inline int adaptive_vrs_refresh() {
	return GLOBAL_GET_CACHED(int, "rendering/fellow/shading/adaptive_vrs_refresh");
}
// Largest error allowed at any pixel of a coarsely shaded tile (luma 0-1), besides the RMS threshold.
inline float adaptive_vrs_max_step() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/shading/adaptive_vrs_max_step");
}
// Motion guard: a tile whose content moved more than this many pixels since the estimated frame shades at most 2 x 2,
// more than twice this at full rate (TAA averages coarse shading over its jittered frames only where content stays).
// 0: off.
inline float adaptive_vrs_motion_px() {
	return GLOBAL_GET_CACHED(float, "rendering/fellow/shading/adaptive_vrs_motion_px");
}
// Tiles where glossy surfaces have varying normals (from the pre-pass's normals and roughness) keep full rate: their
// highlights alias when shaded coarsely.
inline bool adaptive_vrs_gloss_guard() {
	return GLOBAL_GET_CACHED(bool, "rendering/fellow/shading/adaptive_vrs_gloss_guard");
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
	SafeNumeric<uint64_t> raytracing_instances; // ray tracing scene instances built (decision 0080), summed over frames
	SafeNumeric<uint64_t> raytracing_blas_builds; // bottom-level acceleration structures built or rebuilt
	SafeNumeric<uint64_t> raytracing_build_usec; // render thread time building the ray tracing scene (microseconds, summed)
	SafeNumeric<uint64_t> raytracing_tlas_usec; // of which in RenderingDevice::tlas_build()
	SafeNumeric<uint64_t> geometry_cluster_surfaces; // surfaces clustered at creation
	SafeNumeric<uint64_t> geometry_cluster_surfaces_active; // surfaces whose index buffers were put in cluster order
	SafeNumeric<uint64_t> geometry_cluster_surfaces_dropped; // surfaces left in stock order (blending or vertex-moving material)
	SafeNumeric<uint64_t> geometry_cluster_blended_after_activation; // clustered surfaces later set up with a blending material
	SafeNumeric<uint64_t> geometry_cluster_build_usec; // CPU time of cluster builds (worker threads)
	SafeNumeric<uint64_t> geometry_cluster_draws; // draw calls issued for clustered surfaces
	SafeNumeric<uint64_t> pipeline_fallback_draws; // draws with the defaults' pipeline while theirs compiled (decision 0059)
	SafeNumeric<uint64_t> pipeline_ubershader_draws; // draws with the ubershader while theirs compiled (stock's fallback)
	// Where loading spends its time (process totals, every build: the bench snapshots them when its scene is built and
	// after the first frame). Shader versions: the calling thread's start (cache reads, task posting) and wait, the
	// variants compiled on the workers (from source where not cached), the shader objects created from their SPIR-V.
	SafeNumeric<uint64_t> cost_shader_start_usec;
	SafeNumeric<uint64_t> cost_shader_starts;
	SafeNumeric<uint64_t> cost_shader_wait_usec;
	SafeNumeric<uint64_t> cost_shader_waits;
	SafeNumeric<uint64_t> cost_shader_variant_usec;
	SafeNumeric<uint64_t> cost_shader_variants;
	SafeNumeric<uint64_t> cost_shader_cache_loads; // shader version groups read from the shader cache
	SafeNumeric<uint64_t> cost_shader_lazy_creates; // of their variants, created when first asked for (decision 0061)
	SafeNumeric<uint64_t> cost_shader_create_usec;
	SafeNumeric<uint64_t> cost_shader_creates;
	// Pipelines created on the render thread (it waits for them) and on other threads, and the render thread's waits for
	// pipelines being compiled elsewhere.
	SafeNumeric<uint64_t> cost_pipeline_sync_usec;
	SafeNumeric<uint64_t> cost_pipelines_sync;
	SafeNumeric<uint64_t> cost_pipeline_async_usec;
	SafeNumeric<uint64_t> cost_pipelines_async;
	SafeNumeric<uint64_t> cost_pipeline_wait_usec;
	SafeNumeric<uint64_t> cost_pipeline_waits;
	// Reads of GPU data (each flushes the frame and waits for the GPU) and texture creation (with its initial data).
	SafeNumeric<uint64_t> cost_readback_usec;
	SafeNumeric<uint64_t> cost_readbacks;
	SafeNumeric<uint64_t> cost_texture_create_usec;
	SafeNumeric<uint64_t> cost_texture_creates;
	SafeNumeric<uint64_t> geometry_cluster_triangles_drawn; // triangles of clustered surfaces drawn
	SafeNumeric<uint64_t> geometry_cluster_triangles_culled; // triangles of clustered surfaces skipped (outside the frustum)
	SafeNumeric<uint64_t> geometry_cluster_lod_coarser_ranges; // geometry/cluster_lod: ranges drawn at a coarser level than the instance's (decision 0063)
	SafeNumeric<uint64_t> geometry_cluster_lod_reused; // cluster LOD walks reused by a later camera pass of the same render
	SafeNumeric<uint64_t> frame_early_submits; // frames whose rendering was submitted before the swap chain acquisition
	SafeNumeric<uint64_t> frame_async_uses; // async compute segments (decision 0015)
	SafeNumeric<uint64_t> frame_async_conflicts; // of which the concurrent part had to wait (shared resource)
	SafeNumeric<uint64_t> frame_async_transfers; // async parts that used an upload submitted with them
	SafeNumeric<uint64_t> postfx_coherent_gathers; // SSAO/SSIL gather dispatches with one sample rotation per workgroup (decision 0024)
	SafeNumeric<uint64_t> postfx_last_frame_direct; // last-frame copies without the full-size level (decision 0044)
	SafeNumeric<uint64_t> lights_tile_cull_frames; // frames whose opaque colour pass used light tile masks (decision 0049)
	SafeNumeric<uint64_t> lights_tile_cull_split_frames; // of which the pass ran split around the async compute part (v6)
	SafeNumeric<uint64_t> lights_cluster_draws; // draws of the light cluster build's render pass (decision 0051)
	SafeNumeric<uint64_t> lights_cluster_elements; // elements (lights, decals, probes) they drew
	SafeNumeric<uint64_t> lights_cluster_verify_words; // debug/cluster_verify: words compared (decision 0051)
	SafeNumeric<uint64_t> lights_cluster_verify_mismatches;
	SafeNumeric<uint64_t> postfx_luminance_verify_texels; // luminance reduction texels compared (decision 0025, debug)
	SafeNumeric<uint64_t> postfx_luminance_verify_mismatches;
	SafeNumeric<uint64_t> postfx_ssao_temporal_frames; // SSAO gathers with half the taps plus the history (decision 0028)
	SafeNumeric<uint64_t> postfx_ssil_temporal_frames; // SSIL gathers with half the taps plus the history (decision 0030)
	SafeNumeric<uint64_t> postfx_taa_direct_frames; // TAA outputs read by the post-processing chain where the resolve wrote them
	SafeNumeric<uint64_t> occlusion_tested; // opaque list elements tested (debug/occlusion_stats)
	SafeNumeric<uint64_t> occlusion_weight; // their surfaces' indices (full detail)
	SafeNumeric<uint64_t> occlusion_hidden; // elements the pyramid hides
	SafeNumeric<uint64_t> occlusion_hidden_weight;
	SafeNumeric<uint64_t> occlusion_culled; // elements left out (debug/occlusion_cull_approx)
	SafeNumeric<uint64_t> occlusion_cull_passes; // opaque passes culled on the GPU (geometry/occlusion_culling)
	SafeNumeric<uint64_t> occlusion_cull_runs; // runs of elements tested (their draws indirect)
	SafeNumeric<uint64_t> occlusion_cull_elements; // elements tested (read back asynchronously)
	SafeNumeric<uint64_t> occlusion_cull_hidden; // of which hidden (left out of the opaque pass)
	SafeNumeric<uint64_t> occlusion_cull_indices; // indices of the tested elements (at their detail level)
	SafeNumeric<uint64_t> occlusion_cull_hidden_indices; // of which hidden
	SafeNumeric<uint64_t> occlusion_cull_indirect_draws; // draws recorded indirect
	SafeNumeric<uint64_t> occlusion_cull_slot_uploads; // surface bounding boxes uploaded
	SafeNumeric<uint64_t> geometry_multimesh_cull_passes; // culling dispatches (camera passes, shadow passes)
	SafeNumeric<uint64_t> geometry_multimesh_cull_jobs; // MultiMesh instances x passes tested
	SafeNumeric<uint64_t> geometry_multimesh_cull_draws; // draws recorded indirect with the visible instances
	SafeNumeric<uint64_t> geometry_multimesh_instances_tested; // instances tested (read back asynchronously with debug/multimesh_stats)
	SafeNumeric<uint64_t> geometry_multimesh_instances_visible; // of which drawn
	SafeNumeric<uint64_t> geometry_multimesh_instances_coarse; // of which at a coarser level than 0 (per-instance LOD)
	SafeNumeric<uint64_t> geometry_multimesh_jobs_inside; // MultiMeshes left whole: their box inside the pass's volume
	SafeNumeric<uint64_t> geometry_multimesh_sets_small; // pass groups left whole: too few indices to save
	SafeNumeric<uint64_t> geometry_multimesh_pool_copies; // MultiMesh data copied into the pool
	SafeNumeric<uint64_t> geometry_multimesh_pool_grows;
	SafeNumeric<uint64_t> vrs_frames; // frames whose shading rates were set adaptively
	SafeNumeric<uint64_t> vrs_tiles; // shading rate tiles set (read back asynchronously)
	SafeNumeric<uint64_t> vrs_tiles_coarse; // of which coarser than 1 x 1
	SafeNumeric<uint64_t> vrs_pixels; // pixels of those tiles
	SafeNumeric<uint64_t> vrs_fragments; // fragments they shade at their rates (pixels / rate area)
	SafeNumeric<uint64_t> cpu_persistent_elements; // list elements written as 16-byte elements (decision 0022)
	SafeNumeric<uint64_t> cpu_persistent_uploads; // surface entries rewritten by a list element (changed, or first used)
	SafeNumeric<uint64_t> cpu_persistent_maps; // regions of the persistent buffer mapped (one per frame using it)
	SafeNumeric<uint64_t> cpu_persistent_catchup; // entries written into a region when it was mapped (written since its last map)
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
	SafeNumeric<uint64_t> rd_segment_full_barriers; // debug/segment_barriers 2: full barriers at the start of command buffers
	SafeNumeric<uint64_t> rd_graph_segment_hazards; // uses of a resource written in an earlier graph of the frame, with its usage unchanged (no barrier from the usage tracking)
	SafeNumeric<uint64_t> rd_graph_visibility_barriers; // reads given a memory barrier for a stage the last write was not made visible to (decision 0062)
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
	SafeNumeric<uint64_t> autotune_early; // evaluations ended early (a clear difference after 3 pairs)
	SafeNumeric<uint64_t> autotune_rechecks; // evaluations brought forward (the frames' time changed by 20 %, or 30 s passed)
	SafeNumeric<uint64_t> autotune_off_frames; // frames x tunables that ran with the optimization off
	SafeNumeric<uint64_t> autotune_tainted_frames; // evaluation frames not counted: a pipeline or shader was still being created
	SafeNumeric<uint64_t> autotune_prepare_frames; // frames before an evaluation spent compiling its pipelines (decision 0068)
	SafeNumeric<uint64_t> autotune_prepare_requests; // pipelines requested for an evaluation before it starts (decision 0068)
	SafeNumeric<uint64_t> autotune_prepare_timeouts; // evaluations started before their pipelines were all compiled (decision 0068)
	SafeNumeric<uint64_t> frame_textures_cleared; // textures cleared before their first use (frame/clear_new_textures, decision 0070)
	SafeNumeric<uint64_t> frame_textures_cleared_bytes; // their memory
	SafeNumeric<uint64_t> cpu_hot_waits; // low-power waits of idle hot workers (cpu/hot_workers_wait, CPUs with WAITPKG)
	SafeNumeric<uint64_t> cpu_hot_yields; // idle hot workers' yields of their core (cpu/hot_workers_yield)
	SafeNumeric<uint64_t> cpu_hot_lowered; // idle hot workers' drops to the lowest priority (cpu/hot_workers_idle_priority)
};

Stats &stats();

// Adds the time of its scope to a cost counter of stats() (and counts the scope).
struct CostScope {
	SafeNumeric<uint64_t> &usec;
	const uint64_t from;
	CostScope(SafeNumeric<uint64_t> &p_usec, SafeNumeric<uint64_t> &p_count);
	~CostScope();
};

} // namespace Fellow

class Dictionary;
namespace Fellow {
Dictionary get_stats_dictionary();

} // namespace Fellow
