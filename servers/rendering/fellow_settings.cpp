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

#include "core/variant/dictionary.h"
#include "servers/rendering/fellow_parallel.h"

#include <atomic>

void Fellow::register_settings() {
	GLOBAL_DEF("rendering/fellow/enabled", true);
	GLOBAL_DEF("rendering/fellow/postfx/enabled", true);
	GLOBAL_DEF("rendering/fellow/postfx/taa_resolve_v2", true);
	GLOBAL_DEF("rendering/fellow/postfx/taa_history_pingpong", true);
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
	GLOBAL_DEF("rendering/fellow/shadows/max_mip_visible_region", false);
	GLOBAL_DEF("rendering/fellow/shadows/positional_max_mip", true);
	GLOBAL_DEF("rendering/fellow/shadows/debug_disable_caster_culling", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_debug_mode", PROPERTY_HINT_ENUM, "Normal,Lattice Only (No Cache),Rebuild Every Frame"), 0);
	GLOBAL_DEF("rendering/fellow/geometry/enabled", true);
	GLOBAL_DEF("rendering/fellow/geometry/mesh_clusters", true);
	GLOBAL_DEF("rendering/fellow/geometry/cluster_blended_surfaces", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/geometry/cluster_triangles", PROPERTY_HINT_RANGE, "64,65536,1"), 4096);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/geometry/cluster_min_surface_triangles", PROPERTY_HINT_RANGE, "256,1048576,1"), 32768);
	GLOBAL_DEF("rendering/fellow/lights/enabled", true);
	GLOBAL_DEF("rendering/fellow/lights/cluster_depth_slices", true);
	GLOBAL_DEF("rendering/fellow/lights/zero_attenuation_skip", true);
	GLOBAL_DEF("rendering/fellow/lights/cluster_pack_parallel", true);
	GLOBAL_DEF("rendering/fellow/postfx/ssao_shared_tile", false);
	GLOBAL_DEF("rendering/fellow/postfx/taa_camera_motion_vectors", false);
	GLOBAL_DEF("rendering/fellow/shading/blended_alpha_zero_skip", true);
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
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/cpu/worker_tasks", PROPERTY_HINT_RANGE, "0,32,1"), 10);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/cpu/hot_workers_usec", PROPERTY_HINT_RANGE, "0,5000,1,suffix:us"), 300);
	GLOBAL_DEF("rendering/fellow/cpu/hot_workers_gate", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/cpu/hot_workers_wait", PROPERTY_HINT_ENUM, "Pause Loop,UMWAIT C0.1,UMWAIT C0.2"), 0);
	GLOBAL_DEF("rendering/fellow/frame/early_submit", false);
	GLOBAL_DEF("rendering/fellow/frame/present_thread", true);
	GLOBAL_DEF("rendering/fellow/frame/submit_thread", true);
	GLOBAL_DEF("rendering/fellow/frame/parallel_replay", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/frame/replay_max_segments", PROPERTY_HINT_RANGE, "2,64,1"), 12);
	GLOBAL_DEF("rendering/fellow/autotune/enabled", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/autotune/block_frames", PROPERTY_HINT_RANGE, "8,120,1"), 16);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/autotune/min_gain_percent", PROPERTY_HINT_RANGE, "0,10,0.1"), 0.5);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/autotune/interval_frames", PROPERTY_HINT_RANGE, "20,100000,1"), 600);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/autotune/max_interval_sec", PROPERTY_HINT_RANGE, "1,600,0.1,suffix:s"), 30.0);
	GLOBAL_DEF("rendering/fellow/frame/replay_frame_plan", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/frame/replay_plan_min_draws", PROPERTY_HINT_RANGE, "1,4096,1"), 16);
	GLOBAL_DEF_RST("rendering/fellow/frame/separate_present_queue", false);
	GLOBAL_DEF_RST("rendering/fellow/frame/async_compute", false);
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

Fellow::Stats &Fellow::stats() {
	static Stats s;
	return s;
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
	d["geometry_cluster_triangles_drawn"] = s.geometry_cluster_triangles_drawn.get();
	d["geometry_cluster_triangles_culled"] = s.geometry_cluster_triangles_culled.get();
	d["frame_early_submits"] = s.frame_early_submits.get();
	d["frame_async_uses"] = s.frame_async_uses.get();
	d["frame_async_conflicts"] = s.frame_async_conflicts.get();
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
	d["cpu_hot_waits"] = s.cpu_hot_waits.get();
	d["cpu_waitpkg"] = fellow_waitpkg_supported() ? 1 : 0;
	return d;
}
