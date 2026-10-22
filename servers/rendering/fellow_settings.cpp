/**************************************************************************/
/*  fellow_settings.cpp                                                   */
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


#include "fellow_settings.h"

#include "core/config/engine.h"
#include "core/os/os.h"
#include "core/variant/dictionary.h"
#include "servers/rendering/fellow_parallel.h"
#include "servers/rendering/renderer_compositor.h"
#include "servers/rendering/rendering_server_globals.h"

#include <atomic>

void Fellow::register_settings() {
	GLOBAL_DEF("rendering/fellow/enabled", true);
	GLOBAL_DEF("rendering/fellow/postfx/enabled", true);
	GLOBAL_DEF("rendering/fellow/postfx/taa_resolve_v2", true);
	GLOBAL_DEF("rendering/fellow/postfx/taa_tile16", false);
	GLOBAL_DEF("rendering/fellow/animation/elide_modifier_backup", true); // decision 0076 (exact; on from fellow168)
	GLOBAL_DEF("rendering/fellow/animation/full_weight_slerp", true); // decision 0076 (exact; on from fellow168)
	GLOBAL_DEF("rendering/fellow/animation/track_shortcuts", true); // decision 0076 (exact; on from fellow168)
	GLOBAL_DEF("rendering/fellow/postfx/taa_strided_load", false); // decision 0075: REJECT (exact, no gain)
	GLOBAL_DEF("rendering/fellow/postfx/taa_history_pingpong", true);
	// TAA resolve v2: each tile texel's velocity loaded into shared memory with its colour and depth (the closest
	// neighbour's velocity then read from there), and the previous velocity fetched as soon as the reprojection is known
	// (exact; start-up; decision 0044).
	GLOBAL_DEF_RST("rendering/fellow/postfx/taa_shared_velocity", true);
	GLOBAL_DEF("rendering/fellow/postfx/ss_last_frame_direct", true);
	GLOBAL_DEF("rendering/fellow/postfx/taa_direct_output", true);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/postfx/taa_v2_gamma", PROPERTY_HINT_RANGE, "0.5,4,0.01"), 1.25);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/postfx/taa_v2_feedback_min", PROPERTY_HINT_RANGE, "0.5,0.99,0.001"), 0.88);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/postfx/taa_v2_feedback_max", PROPERTY_HINT_RANGE, "0.5,0.99,0.001"), 0.9375);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/postfx/taa_v2_antiflicker", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.0);
	GLOBAL_DEF("rendering/fellow/postfx/taa_v2_velocity_dilation", true);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/postfx/taa_v2_motion_soften", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.35);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/postfx/taa_v2_soften_contrast", PROPERTY_HINT_RANGE, "0.01,2,0.01"), 0.45);
	GLOBAL_DEF("rendering/fellow/fluid/enabled", true);
	GLOBAL_DEF("rendering/fellow/profiling/pass_timestamps", false);
	GLOBAL_DEF("rendering/fellow/profiling/list_markers", false);
	GLOBAL_DEF("rendering/fellow/shadows/enabled", true);
	GLOBAL_DEF("rendering/fellow/shadows/directional_cache", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_guard_texels", PROPERTY_HINT_RANGE, "16,1024,16"), 256);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_static_frames", PROPERTY_HINT_RANGE, "1,600,1"), 60);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/shadows/directional_cache_lod_travel", PROPERTY_HINT_RANGE, "0,50,0.1,suffix:m"), 0.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/shadows/directional_cache_texel_lod", PROPERTY_HINT_RANGE, "0,8,0.05"), 1.0);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_max_rebuilds_per_frame", PROPERTY_HINT_RANGE, "1,16,1"), 1);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/shadows/directional_cache_min_cost_per_texel", PROPERTY_HINT_RANGE, "0,4,0.005"), 0.125);
	GLOBAL_DEF("rendering/fellow/shadows/skip_backfacing_directional", true);
	GLOBAL_DEF("rendering/fellow/shadows/skip_unblended_cascade", true);
	GLOBAL_DEF("rendering/fellow/shadows/skip_backfacing_positional", true);
	GLOBAL_DEF("rendering/fellow/shadows/pcss_blend_band", false);
	GLOBAL_DEF("rendering/fellow/shadows/pcss_directional_penumbra", false);
	GLOBAL_DEF("rendering/fellow/shadows/directional_max_mip", true);
	GLOBAL_DEF("rendering/fellow/shadows/atlas_simulated_time", true); // decision 0071
	GLOBAL_DEF("rendering/fellow/shadows/pcss_filter_early_out", false);
	GLOBAL_DEF("rendering/fellow/shadows/batched_taps", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/debug/batched_taps_kinds", PROPERTY_HINT_FLAGS, "Directional,Positional"), 3);
	GLOBAL_DEF("rendering/fellow/shadows/pcss_quad_search", false);
	GLOBAL_DEF("rendering/fellow/shadows/max_mip_visible_region", false);
	GLOBAL_DEF("rendering/fellow/shadows/positional_max_mip", true);
	GLOBAL_DEF("rendering/fellow/shadows/debug_disable_caster_culling", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_debug_mode", PROPERTY_HINT_ENUM, "Normal,Lattice Only (No Cache),Rebuild Every Frame"), 0);
	GLOBAL_DEF("rendering/fellow/geometry/enabled", true);
	GLOBAL_DEF("rendering/fellow/geometry/mesh_clusters", true);
	GLOBAL_DEF("rendering/fellow/geometry/cluster_blended_surfaces", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/geometry/cluster_triangles", PROPERTY_HINT_RANGE, "64,65536,1"), 4096);
	GLOBAL_DEF("rendering/fellow/frame/pipeline_fallback", false); // decision 0059 (rejected, off)
	GLOBAL_DEF("rendering/fellow/frame/depth_pipeline_keys", true); // decision 0069
	GLOBAL_DEF("rendering/fellow/frame/clear_new_textures", true); // decision 0070
	GLOBAL_DEF("rendering/fellow/frame/movie_wait_pipelines", true); // decision 0072
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/frame/async_ss_split", PROPERTY_HINT_ENUM, "Stock,SSIL On Main Queue,SSIL And SSR On Main Queue,Autotune"), 3); // decision 0060 (3 from fellow147)
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/geometry/cluster_front_to_back", PROPERTY_HINT_RANGE, "0,1048576,1"), 0); // decision 0058 (rejected, off)
	GLOBAL_DEF("rendering/fellow/geometry/cluster_lod", false); // decision 0063 (changes the image; off until measured)
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/geometry/cluster_min_surface_triangles", PROPERTY_HINT_RANGE, "256,1048576,1"), 32768);
	GLOBAL_DEF("rendering/fellow/lights/enabled", true);
	GLOBAL_DEF("rendering/fellow/lights/cluster_depth_slices", true);
	GLOBAL_DEF("rendering/fellow/lights/zero_attenuation_skip", true);
	GLOBAL_DEF("rendering/fellow/lights/cluster_pack_parallel", true);
	GLOBAL_DEF("rendering/fellow/lights/cluster_pack_elements", true);
	GLOBAL_DEF("rendering/fellow/lights/cluster_instanced_draws", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/debug/cluster_verify", PROPERTY_HINT_ENUM, "Off,Verify,Verify With Negative Control"), 0);
	GLOBAL_DEF("rendering/fellow/debug/cluster_keep_depth_words", false);
	GLOBAL_DEF("rendering/fellow/lights/fields_early", true);
	GLOBAL_DEF("rendering/fellow/lights/tile_cull", true); // under autotune: off until measured faster (decision 0049)
	// Diagnostics: light tile culling keeps every candidate (the opaque colour pass's code path, without its effect).
	GLOBAL_DEF("rendering/fellow/debug/tile_cull_passthrough", false);
	// Diagnostics: light tile culling's pass runs, but the opaque colour pass does not read its masks.
	GLOBAL_DEF("rendering/fellow/debug/tile_cull_pass_only", false);
	// Diagnostics: light tile culling split around the async compute part (0 when async compute runs, 1 always, 2 never).
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/debug/tile_cull_split", PROPERTY_HINT_ENUM, "With Async Compute,Always,Never"), 0);
	// Render graph: a memory barrier for a resource used again, with the same usage, after a graph (command buffer)
	// of the same frame that wrote it (async compute and early submission record a frame as several graphs).
	// (2: also a full barrier at the start of each such command buffer on the main queue: diagnostics.)
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/debug/segment_barriers", PROPERTY_HINT_ENUM, "Off,Resources,Resources And Full"), 1);
	GLOBAL_DEF("rendering/fellow/debug/visibility_barriers", true); // decision 0062
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/lights/hot_fields_buffer", PROPERTY_HINT_ENUM, "Off,Storage Buffer,Uniform Buffer"), 0);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/lights/prefetch_next", PROPERTY_HINT_ENUM, "Off,Range Test Fields,All Hot Fields,Range Test Fields And Words,All Hot Fields And Words"), 0);
	GLOBAL_DEF("rendering/fellow/postfx/ssao_shared_tile", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/postfx/glow_blur", PROPERTY_HINT_ENUM, "Stock,Padded,Tiles 16x16"), 2); // decision 0052
	GLOBAL_DEF("rendering/fellow/postfx/ssr_filter_padded", false); // decision 0052: no gain (m10_f151)
	GLOBAL_DEF("rendering/fellow/postfx/tonemap_specialized", true); // decision 0055 (on from fellow138)
	GLOBAL_DEF("rendering/fellow/postfx/fog_specialized", true); // decision 0056 (on from fellow138)
	// Bits: 1 no decals, 2 no reflection probes, 4 no directional lights, 8 no omni lights, 16 no spot lights, 32 no
	// shadowed omni or spot light (63: all; decision 0057, measuring).
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shading/absent_features_specialized", PROPERTY_HINT_FLAGS, "Decals,Reflection Probes,Directional Lights,Omni Lights,Spot Lights,Positional Shadows"), 63); // decision 0057 (all six on from fellow142)
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/postfx/fog_light_loop", PROPERTY_HINT_ENUM, "Stock,Subgroup,Subgroup 2x2x16,Stock 2x2x16"), 0); // decision 0053 (rejected, off)
	GLOBAL_DEF("rendering/fellow/postfx/ss_coherent_gather", false);
	GLOBAL_DEF("rendering/fellow/postfx/fast_luminance_reduce", true);
	// SSAO/SSIL interleave: the output image's size as the bound (decision 0067; stock leaves the last column unwritten at
	// widths such as 1920).
	GLOBAL_DEF("rendering/fellow/postfx/ss_interleave_exact_bounds", true);
	// SSAO/SSIL depth mipmaps: one store per texel of the last two levels (decision 0067; stock races).
	GLOBAL_DEF("rendering/fellow/postfx/ss_depth_mips_exact", true);
	GLOBAL_DEF("rendering/fellow/postfx/ssr_resolve_shared", false); // decision 0074 (measuring)
	GLOBAL_DEF("rendering/fellow/postfx/ssao_temporal", false);
	GLOBAL_DEF("rendering/fellow/postfx/ssil_temporal", false);
	// SSAO/SSIL gathers: taps whose texture fetches are issued together (decision 0041; 0 or 1 = stock's loop). Start-up.
	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "rendering/fellow/postfx/ss_tap_batch", PROPERTY_HINT_RANGE, "0,8,1"), 0);
	GLOBAL_DEF("rendering/fellow/postfx/fog_half_rate", false);
	GLOBAL_DEF("rendering/fellow/postfx/verify_luminance_reduce", false);
	GLOBAL_DEF("rendering/fellow/geometry/occlusion_culling", false);
	GLOBAL_DEF("rendering/fellow/geometry/multimesh_culling", true);
	GLOBAL_DEF("rendering/fellow/geometry/multimesh_instance_lod", false);
	GLOBAL_DEF("rendering/fellow/geometry/multimesh_cull_vertex_motion", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/geometry/multimesh_cull_min_instances", PROPERTY_HINT_RANGE, "1,65536,1"), 64);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/geometry/multimesh_cull_min_indices", PROPERTY_HINT_RANGE, "0,100000000,1"), 300000);
	GLOBAL_DEF("rendering/fellow/geometry/prepass_simple_first", false);
	GLOBAL_DEF("rendering/fellow/debug/occlusion_stats", false);
	GLOBAL_DEF("rendering/fellow/debug/multimesh_stats", false);
	// Diagnostics: experiments on the compiled scene shaders (e.g. their register counts with FELLOW_PIPELINE_STATS), bits
	// read at start-up: 1 FELLOW_EXP_DONT_UNROLL (loops bounded by specialization constants not unrolled), 2
	// FELLOW_EXP_UNROLL (unrolled), 4 directional soft shadow cascades in one loop, 8 shadow filter path shares written as
	// the colour (positional; with 16 directional), 32 shadow pyramid windows one level finer, 64 directional PCSS returns
	// lit at once with its code kept (images change), 128 directional PCSS early-out after the blocker search, 256
	// tap batches of 2 instead of 4 (decision 0040; 512 batches of 6), 1024 the directional PCSS loops unrolled
	// (with shadows/batched_taps off), 2048 a blended pixel's two PCSS cascades with their taps fetched together, 4096 no
	// omni/spot lights and 8192 no decals/reflection probes in the colour pass (code kept; images change), 16384 the
	// quad-shared PCSS search only within a small light-space depth range, 32768 omni/spot lights that cannot reach the
	// pixel dropped from each cluster word first, four at a time, 65536 no light_compute() for omni/spot lights, 131072 no
	// omni/spot shadow lookups (code kept; images change), 262144 with 8 light counts as the colour (with 32: lights within
	// range), 524288 omni/spot lights stop after their range test, 1048576 the positional lights' PCF loops in one batch
	// of 8, 2097152 omni/spot shadow fields read after the range test, 4194304 discard as demote to helper (derivatives
	// of quads with discarded invocations defined: for image tests on lavapipe). 0: none (the shipped shaders).
	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "rendering/fellow/debug/scene_shader_experiments", PROPERTY_HINT_RANGE, "0,8388607,1"), 0);
	// Diagnostics: the light cluster tile's side in pixels (8, 16, 32 = stock, 64), when the render buffers are set up.
	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "rendering/fellow/debug/cluster_size", PROPERTY_HINT_ENUM, "8:8,16:16,32:32,64:64"), 32);
	GLOBAL_DEF("rendering/fellow/debug/occlusion_cull_approx", false);
	GLOBAL_DEF("rendering/fellow/postfx/taa_camera_motion_vectors", false);
	GLOBAL_DEF("rendering/fellow/shading/blended_alpha_zero_skip", true);
	GLOBAL_DEF_RST("rendering/fellow/shading/adaptive_vrs", false);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/shading/adaptive_vrs_threshold", PROPERTY_HINT_RANGE, "0,0.1,0.0005"), 0.006);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shading/adaptive_vrs_max_rate", PROPERTY_HINT_ENUM, "2x2,4x4"), 1);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shading/adaptive_vrs_refresh", PROPERTY_HINT_RANGE, "0,64,1"), 8);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/shading/adaptive_vrs_max_step", PROPERTY_HINT_RANGE, "0,0.5,0.001"), 0.02);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/shading/adaptive_vrs_motion_px", PROPERTY_HINT_RANGE, "0,64,0.5,suffix:px"), 0.0);
	GLOBAL_DEF("rendering/fellow/shading/adaptive_vrs_gloss_guard", true);
	GLOBAL_DEF("rendering/fellow/shading/adaptive_vrs_autotune", true);
	// Render-thread CPU (decisions 0016-0018): on by default since fellow56 (exact: same lists, instance data, draws and
	// commands as stock; measured in City Lab, its draw-heavy variant and the GPU-bound scenes).
	GLOBAL_DEF("rendering/fellow/cpu/enabled", true);
	GLOBAL_DEF("rendering/fellow/cpu/parallel_list_fill", true);
	GLOBAL_DEF("rendering/fellow/cpu/parallel_instance_data", true);
	GLOBAL_DEF("rendering/fellow/cpu/compact_list_sort", true);
	GLOBAL_DEF("rendering/fellow/cpu/parallel_list_sort", true);
	GLOBAL_DEF("rendering/fellow/cpu/parallel_partition", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/cpu/parallel_partition_min", PROPERTY_HINT_RANGE, "1024,1048576,1"), 4096);
	GLOBAL_DEF("rendering/fellow/cpu/parallel_recording", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/cpu/recording_segments_per_thread", PROPERTY_HINT_RANGE, "1,8,1"), 1);
	GLOBAL_DEF("rendering/fellow/cpu/parallel_cull", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/cpu/cull_ranges_per_thread", PROPERTY_HINT_RANGE, "1,16,1"), 1);
	GLOBAL_DEF("rendering/fellow/cpu/prefetch", false);
	GLOBAL_DEF("rendering/fellow/cpu/batched_shadow_lists", true);
	GLOBAL_DEF("rendering/fellow/cpu/compact_shadow_instances", true);
	GLOBAL_DEF("rendering/fellow/cpu/verify", false);
	GLOBAL_DEF("rendering/fellow/cpu/debug_instance_scratch", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/cpu/instance_buffer_memory", PROPERTY_HINT_ENUM, "Device (Stock),System Memory,System Memory Cached"), 0);
	GLOBAL_DEF("rendering/fellow/cpu/persistent_instances", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/cpu/worker_tasks", PROPERTY_HINT_RANGE, "-1,32,1"), -1); // decision 0073 (-1: automatic, 6 on the benchmark machine; 10 before)
	// 30 us from fellow156 (decision 0065; was 300): with 300 a CPU-bound frame's hot workers spin through the whole frame
	// and presentation stalls for a scheduler quantum (City Lab: 16-45 ms frames).
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/cpu/hot_workers_usec", PROPERTY_HINT_RANGE, "0,5000,1,suffix:us"), 30);
	GLOBAL_DEF("rendering/fellow/cpu/hot_workers_gate", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/cpu/hot_workers_wait", PROPERTY_HINT_ENUM, "Pause Loop,UMWAIT C0.1,UMWAIT C0.2"), 0);
	GLOBAL_DEF("rendering/fellow/cpu/hot_workers_yield", false); // decision 0065 (experiment: City Lab +7.9 %, spikes left)
	GLOBAL_DEF("rendering/fellow/cpu/hot_workers_idle_priority", false); // decision 0065
	GLOBAL_DEF("rendering/fellow/frame/early_submit", false);
	GLOBAL_DEF("rendering/fellow/frame/present_thread", true);
	GLOBAL_DEF("rendering/fellow/frame/parallel_shader_create", true); // decision 0061
	GLOBAL_DEF("rendering/fellow/frame/lazy_shader_variants", true); // decision 0061
	GLOBAL_DEF("rendering/fellow/frame/submit_thread", true);
	GLOBAL_DEF("rendering/fellow/frame/parallel_replay", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/frame/replay_max_segments", PROPERTY_HINT_RANGE, "2,64,1"), 12);
	GLOBAL_DEF("rendering/fellow/autotune/enabled", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/autotune/block_frames", PROPERTY_HINT_RANGE, "8,120,1"), 16);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/autotune/min_gain_percent", PROPERTY_HINT_RANGE, "0,10,0.1"), 0.5);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/autotune/interval_frames", PROPERTY_HINT_RANGE, "20,100000,1"), 600);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/autotune/max_interval_sec", PROPERTY_HINT_RANGE, "1,600,0.1,suffix:s"), 30.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/autotune/max_duty_percent", PROPERTY_HINT_RANGE, "1,100,1"), 20.0);
	GLOBAL_DEF("rendering/fellow/autotune/skip_creation_frames", true); // decision 0064
	GLOBAL_DEF("rendering/fellow/autotune/continue_undecided", true); // decision 0064
	GLOBAL_DEF("rendering/fellow/autotune/prepare_pipelines", true); // decision 0068
	GLOBAL_DEF("rendering/fellow/frame/replay_frame_plan", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/frame/replay_plan_min_draws", PROPERTY_HINT_RANGE, "1,4096,1"), 16);
	GLOBAL_DEF_RST("rendering/fellow/frame/separate_present_queue", false);
	// 0 off, 1 on, 2 on NVIDIA GPUs (decision 0015; read when the device is created, so only a value set in project.godot or
	// override.cfg counts, and 2 when there is none).
	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "rendering/fellow/frame/async_compute", PROPERTY_HINT_ENUM, "Off,On,NVIDIA GPUs"), 2);
	// The pipeline cache file is ignored at start-up (and replaced by the next save) when larger than this: stock lets
	// it grow without bound, and at 2 GiB its checksum's length overflows and the save crashes (decision 0050).
	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "rendering/fellow/frame/pipeline_cache_max_mb", PROPERTY_HINT_RANGE, "64,2000,1,suffix:MiB"), 1024);
	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "rendering/fellow/frame/async_compute_family", PROPERTY_HINT_ENUM, "Dedicated Compute Family,Main Family,Main Queue (Split Only)"), 0);
}

// Hot workers' gate (render thread only): exponential averages of the fence wait and the frame interval; the gate
// closes when the waits exceed 10 % of the frame and opens again below 3 % (hysteresis: a frame that just turned
// CPU-bound by going hot does not flip back and forth).
static double fellow_gate_wait_avg = 0.0;
static double fellow_gate_frame_avg = 0.0;
static std::atomic<bool> fellow_gate_open{ true };

bool Fellow::cpu_hot_workers_gate_open() {
	return fellow_gate_open.load(std::memory_order_relaxed);
}

void Fellow::frame_gpu_wait_report(uint64_t p_wait_usec, uint64_t p_frame_usec) {
	stats().rd_fence_wait_usec.add(p_wait_usec);
	const double a = 1.0 / 16.0;
	fellow_gate_wait_avg += (double(p_wait_usec) - fellow_gate_wait_avg) * a;
	fellow_gate_frame_avg += (double(MIN(p_frame_usec, uint64_t(1000000))) - fellow_gate_frame_avg) * a;
	const bool open = fellow_gate_open.load(std::memory_order_relaxed);
	if (open && fellow_gate_wait_avg > 0.10 * fellow_gate_frame_avg) {
		fellow_gate_open.store(false, std::memory_order_relaxed);
	} else if (!open && fellow_gate_wait_avg < 0.03 * fellow_gate_frame_avg) {
		fellow_gate_open.store(true, std::memory_order_relaxed);
	}
	if (!fellow_gate_open.load(std::memory_order_relaxed)) {
		stats().cpu_hot_gate_closed_frames.increment();
	}
}

uint64_t Fellow::shadow_atlas_tick_msec() {
	const int fps = Engine::get_singleton()->fellow_get_fixed_fps();
	if (fps > 0 && shadows_enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/shadows/atlas_simulated_time") && RSG::rasterizer) {
		return RSG::rasterizer->get_frame_number() * 1000 / uint64_t(fps);
	}
	return OS::get_singleton()->get_ticks_msec();
}

bool Fellow::frame_movie_wait_pipelines() {
	static const bool writing_movie = !Engine::get_singleton()->get_write_movie_path().is_empty();
	return writing_movie && enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/movie_wait_pipelines");
}

int Fellow::cpu_worker_tasks_auto() {
	static const int tasks = CLAMP(OS::get_singleton()->get_processor_count() / 2 - 2, 1, 6);
	return tasks;
}

Fellow::Stats &Fellow::stats() {
	static Stats s;
	return s;
}

Fellow::CostScope::CostScope(SafeNumeric<uint64_t> &p_usec, SafeNumeric<uint64_t> &p_count) :
		usec(p_usec), from(OS::get_singleton()->get_ticks_usec()) {
	p_count.increment();
}

Fellow::CostScope::~CostScope() {
	usec.add(OS::get_singleton()->get_ticks_usec() - from);
}

Dictionary Fellow::get_stats_dictionary() {
	Stats &s = stats();
	Dictionary d;
	d["shadow_cache_cascades_stock"] = s.shadow_cache_cascades_stock.get();
	d["shadow_cache_cascades_fallback"] = s.shadow_cache_cascades_fallback.get();
	d["shadow_cache_cascades_cached"] = s.shadow_cache_cascades_cached.get();
	d["shadow_cache_cascades_rebuilt"] = s.shadow_cache_cascades_rebuilt.get();
	d["shadow_cache_rebuilds_forced"] = s.shadow_cache_rebuilds_forced.get();
	d["shadow_cache_rebuilds_proactive"] = s.shadow_cache_rebuilds_proactive.get();
	d["shadow_cache_rebuilds_promotion"] = s.shadow_cache_rebuilds_promotion.get();
	d["shadow_cache_invalidations"] = s.shadow_cache_invalidations.get();
	d["shadow_cache_dynamic_casters"] = s.shadow_cache_dynamic_casters.get();
	d["shadow_cache_rebuild_casters"] = s.shadow_cache_rebuild_casters.get();
	d["shadow_cache_frames"] = s.shadow_cache_frames.get();
	d["shadow_cache_copies_skipped"] = s.shadow_cache_copies_skipped.get();
	d["shadow_cache_cascades_uneconomic"] = s.shadow_cache_cascades_uneconomic.get();
	d["shadow_cache_cascades_unstable"] = s.shadow_cache_cascades_unstable.get();
	d["geometry_cluster_surfaces"] = s.geometry_cluster_surfaces.get();
	d["geometry_cluster_surfaces_active"] = s.geometry_cluster_surfaces_active.get();
	d["geometry_cluster_surfaces_dropped"] = s.geometry_cluster_surfaces_dropped.get();
	d["geometry_cluster_blended_after_activation"] = s.geometry_cluster_blended_after_activation.get();
	d["geometry_cluster_build_usec"] = s.geometry_cluster_build_usec.get();
	d["geometry_cluster_draws"] = s.geometry_cluster_draws.get();
	d["pipeline_fallback_draws"] = s.pipeline_fallback_draws.get();
	d["pipeline_ubershader_draws"] = s.pipeline_ubershader_draws.get();
	d["cost_shader_start_usec"] = s.cost_shader_start_usec.get();
	d["cost_shader_starts"] = s.cost_shader_starts.get();
	d["cost_shader_wait_usec"] = s.cost_shader_wait_usec.get();
	d["cost_shader_waits"] = s.cost_shader_waits.get();
	d["cost_shader_variant_usec"] = s.cost_shader_variant_usec.get();
	d["cost_shader_variants"] = s.cost_shader_variants.get();
	d["cost_shader_cache_loads"] = s.cost_shader_cache_loads.get();
	d["cost_shader_lazy_creates"] = s.cost_shader_lazy_creates.get();
	d["cost_shader_create_usec"] = s.cost_shader_create_usec.get();
	d["cost_shader_creates"] = s.cost_shader_creates.get();
	d["cost_pipeline_sync_usec"] = s.cost_pipeline_sync_usec.get();
	d["cost_pipelines_sync"] = s.cost_pipelines_sync.get();
	d["cost_pipeline_async_usec"] = s.cost_pipeline_async_usec.get();
	d["cost_pipelines_async"] = s.cost_pipelines_async.get();
	d["cost_pipeline_wait_usec"] = s.cost_pipeline_wait_usec.get();
	d["cost_pipeline_waits"] = s.cost_pipeline_waits.get();
	d["cost_readback_usec"] = s.cost_readback_usec.get();
	d["cost_readbacks"] = s.cost_readbacks.get();
	d["cost_texture_create_usec"] = s.cost_texture_create_usec.get();
	d["cost_texture_creates"] = s.cost_texture_creates.get();
	d["geometry_cluster_triangles_drawn"] = s.geometry_cluster_triangles_drawn.get();
	d["geometry_cluster_triangles_culled"] = s.geometry_cluster_triangles_culled.get();
	d["geometry_cluster_lod_coarser_ranges"] = s.geometry_cluster_lod_coarser_ranges.get();
	d["geometry_cluster_lod_reused"] = s.geometry_cluster_lod_reused.get();
	d["frame_early_submits"] = s.frame_early_submits.get();
	d["frame_async_uses"] = s.frame_async_uses.get();
	d["frame_async_conflicts"] = s.frame_async_conflicts.get();
	d["frame_async_transfers"] = s.frame_async_transfers.get();
	d["postfx_coherent_gathers"] = s.postfx_coherent_gathers.get();
	d["postfx_last_frame_direct"] = s.postfx_last_frame_direct.get();
	d["lights_tile_cull_frames"] = s.lights_tile_cull_frames.get();
	d["lights_tile_cull_split_frames"] = s.lights_tile_cull_split_frames.get();
	d["lights_cluster_draws"] = s.lights_cluster_draws.get();
	d["lights_cluster_elements"] = s.lights_cluster_elements.get();
	d["lights_cluster_verify_words"] = s.lights_cluster_verify_words.get();
	d["lights_cluster_verify_mismatches"] = s.lights_cluster_verify_mismatches.get();
	d["postfx_luminance_verify_texels"] = s.postfx_luminance_verify_texels.get();
	d["postfx_luminance_verify_mismatches"] = s.postfx_luminance_verify_mismatches.get();
	d["postfx_ssao_temporal_frames"] = s.postfx_ssao_temporal_frames.get();
	d["postfx_ssil_temporal_frames"] = s.postfx_ssil_temporal_frames.get();
	d["postfx_taa_direct_frames"] = s.postfx_taa_direct_frames.get();
	d["occlusion_tested"] = s.occlusion_tested.get();
	d["occlusion_weight"] = s.occlusion_weight.get();
	d["occlusion_hidden"] = s.occlusion_hidden.get();
	d["occlusion_hidden_weight"] = s.occlusion_hidden_weight.get();
	d["occlusion_culled"] = s.occlusion_culled.get();
	d["occlusion_cull_passes"] = s.occlusion_cull_passes.get();
	d["occlusion_cull_runs"] = s.occlusion_cull_runs.get();
	d["occlusion_cull_elements"] = s.occlusion_cull_elements.get();
	d["occlusion_cull_hidden"] = s.occlusion_cull_hidden.get();
	d["occlusion_cull_indices"] = s.occlusion_cull_indices.get();
	d["occlusion_cull_hidden_indices"] = s.occlusion_cull_hidden_indices.get();
	d["occlusion_cull_indirect_draws"] = s.occlusion_cull_indirect_draws.get();
	d["occlusion_cull_slot_uploads"] = s.occlusion_cull_slot_uploads.get();
	d["geometry_multimesh_cull_passes"] = s.geometry_multimesh_cull_passes.get();
	d["geometry_multimesh_cull_jobs"] = s.geometry_multimesh_cull_jobs.get();
	d["geometry_multimesh_cull_draws"] = s.geometry_multimesh_cull_draws.get();
	d["geometry_multimesh_instances_tested"] = s.geometry_multimesh_instances_tested.get();
	d["geometry_multimesh_instances_visible"] = s.geometry_multimesh_instances_visible.get();
	d["geometry_multimesh_instances_coarse"] = s.geometry_multimesh_instances_coarse.get();
	d["geometry_multimesh_jobs_inside"] = s.geometry_multimesh_jobs_inside.get();
	d["geometry_multimesh_sets_small"] = s.geometry_multimesh_sets_small.get();
	d["geometry_multimesh_pool_copies"] = s.geometry_multimesh_pool_copies.get();
	d["geometry_multimesh_pool_grows"] = s.geometry_multimesh_pool_grows.get();
	d["vrs_frames"] = s.vrs_frames.get();
	d["vrs_tiles"] = s.vrs_tiles.get();
	d["vrs_tiles_coarse"] = s.vrs_tiles_coarse.get();
	d["vrs_pixels"] = s.vrs_pixels.get();
	d["vrs_fragments"] = s.vrs_fragments.get();
	d["cpu_persistent_elements"] = s.cpu_persistent_elements.get();
	d["cpu_persistent_uploads"] = s.cpu_persistent_uploads.get();
	d["cpu_persistent_maps"] = s.cpu_persistent_maps.get();
	d["cpu_persistent_catchup"] = s.cpu_persistent_catchup.get();
	d["shadow_pyramid_builds"] = s.shadow_pyramid_builds.get();
	d["shadow_pyramid_texels"] = s.shadow_pyramid_texels.get();
	d["shadow_pyramid_texels_needed"] = s.shadow_pyramid_texels_needed.get();
	d["cpu_shadow_batches"] = s.cpu_shadow_batches.get();
	d["cpu_shadow_batch_passes"] = s.cpu_shadow_batch_passes.get();
	d["cpu_shadow_batch_fallbacks"] = s.cpu_shadow_batch_fallbacks.get();
	d["cpu_verify_checks"] = s.cpu_verify_checks.get();
	d["cpu_verify_mismatches"] = s.cpu_verify_mismatches.get();
	d["cpu_loops"] = s.cpu_loops.get();
	d["cpu_loop_chunks"] = s.cpu_loop_chunks.get();
	d["cpu_helper_chunks"] = s.cpu_helper_chunks.get();
	d["rd_frames"] = s.rd_frames.get();
	d["rd_graph_end_usec"] = s.rd_graph_end_usec.get();
	d["rd_execute_usec"] = s.rd_execute_usec.get();
	d["rd_fence_wait_usec"] = s.rd_fence_wait_usec.get();
	d["rd_vk_submit_usec"] = s.rd_vk_submit_usec.get();
	d["rd_vk_present_usec"] = s.rd_vk_present_usec.get();
	d["rd_vk_presents"] = s.rd_vk_presents.get();
	d["frame_present_thread_uses"] = s.frame_present_thread_uses.get();
	d["frame_present_thread_wait_usec"] = s.frame_present_thread_wait_usec.get();
	d["frame_replay_lists"] = s.frame_replay_lists.get();
	d["frame_replay_segments"] = s.frame_replay_segments.get();
	d["rd_graph_commands"] = s.rd_graph_commands.get();
	d["rd_graph_sort_nsec"] = s.rd_graph_sort_nsec.get();
	d["rd_graph_barriers_nsec"] = s.rd_graph_barriers_nsec.get();
	d["rd_graph_segment_hazards"] = s.rd_graph_segment_hazards.get();
	d["rd_graph_visibility_barriers"] = s.rd_graph_visibility_barriers.get();
	d["rd_segment_full_barriers"] = s.rd_segment_full_barriers.get();
	d["rd_graph_draw_nsec"] = s.rd_graph_draw_nsec.get();
	d["rd_graph_draw_lists"] = s.rd_graph_draw_lists.get();
	d["rd_graph_draw_bytes"] = s.rd_graph_draw_bytes.get();
	d["cpu_hot_gate_closed_frames"] = s.cpu_hot_gate_closed_frames.get();
	d["frame_submit_jobs"] = s.frame_submit_jobs.get();
	d["rd_async_end_usec"] = s.rd_async_end_usec.get();
	d["rd_async_submit_usec"] = s.rd_async_submit_usec.get();
	d["cpu_record_lists"] = s.cpu_record_lists.get();
	d["cpu_record_segments"] = s.cpu_record_segments.get();
	d["cpu_record_fallbacks"] = s.cpu_record_fallbacks.get();
	d["frame_replay_recorded_lists"] = s.frame_replay_recorded_lists.get();
	d["frame_plan_lists"] = s.frame_plan_lists.get();
	d["frame_plan_segments"] = s.frame_plan_segments.get();
	d["frame_plan_wait_nsec"] = s.frame_plan_wait_nsec.get();
	d["autotune_evals"] = s.autotune_evals.get();
	d["autotune_flips"] = s.autotune_flips.get();
	d["autotune_early"] = s.autotune_early.get();
	d["autotune_rechecks"] = s.autotune_rechecks.get();
	d["autotune_off_frames"] = s.autotune_off_frames.get();
	d["autotune_tainted_frames"] = s.autotune_tainted_frames.get();
	d["autotune_prepare_frames"] = s.autotune_prepare_frames.get();
	d["autotune_prepare_requests"] = s.autotune_prepare_requests.get();
	d["autotune_prepare_timeouts"] = s.autotune_prepare_timeouts.get();
	d["frame_textures_cleared"] = s.frame_textures_cleared.get();
	d["frame_textures_cleared_bytes"] = s.frame_textures_cleared_bytes.get();
	d["cpu_hot_waits"] = s.cpu_hot_waits.get();
	d["cpu_hot_yields"] = s.cpu_hot_yields.get();
	d["cpu_hot_lowered"] = s.cpu_hot_lowered.get();
	d["cpu_waitpkg"] = fellow_waitpkg_supported() ? 1 : 0;
	return d;
}
