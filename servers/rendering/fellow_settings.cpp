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
#include "servers/rendering/rendering_device.h"
#include "servers/rendering/rendering_server_default.h"
#include "servers/rendering/rendering_server_globals.h"

#include <atomic>

void Fellow::register_settings() {
	GLOBAL_DEF("rendering/fellow/enabled", true);
	GLOBAL_DEF("rendering/fellow/postfx/enabled", true);
	GLOBAL_DEF("rendering/fellow/postfx/taa_resolve_v2", true);
	GLOBAL_DEF("rendering/fellow/postfx/taa_tile16", false);
	// Visual features (milestone 16 on): off by default, they add to the image what the project turns on.
	GLOBAL_DEF("rendering/fellow/post/motion_blur/enabled", false); // decision 0078
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/motion_blur/amount", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.5); // fraction of the frame the shutter is open
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/post/motion_blur/samples", PROPERTY_HINT_RANGE, "2,64,1"), 12);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/motion_blur/max_radius", PROPERTY_HINT_RANGE, "1,32,0.5,suffix:px"), 32.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/sharpen/strength", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.0); // decision 0110
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/bloom/lens_dirt", PROPERTY_HINT_RANGE, "0,20,0.01"), 0.0); // decision 0111
	GLOBAL_DEF("rendering/fellow/post/bloom/enabled", false); // decision 0112
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/bloom/intensity", PROPERTY_HINT_RANGE, "0,1,0.001"), 0.05);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/bloom/scatter", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.7);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/bloom/lens_flare", PROPERTY_HINT_RANGE, "0,10,0.01"), 0.0); // decision 0113
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/bloom/lens_flare_threshold", PROPERTY_HINT_RANGE, "0,100000,0.1,or_greater"), 8.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/light_shafts/intensity", PROPERTY_HINT_RANGE, "0,10,0.01"), 0.0); // decision 0116
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/light_shafts/threshold", PROPERTY_HINT_RANGE, "0,100,0.01,or_greater"), 0.5);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/light_shafts/length", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.9);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/white_balance/temperature", PROPERTY_HINT_RANGE, "1500,15000,10,suffix:K"), 6500.0); // decision 0114
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/white_balance/tint", PROPERTY_HINT_RANGE, "-1,1,0.01"), 0.0);
	GLOBAL_DEF("rendering/fellow/post/clouds/enabled", false); // decision 0109
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/clouds/coverage", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.45);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/clouds/density", PROPERTY_HINT_RANGE, "0,5,0.01"), 1.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/clouds/altitude", PROPERTY_HINT_RANGE, "0.1,10,0.1,suffix:km"), 1.5);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/clouds/thickness", PROPERTY_HINT_RANGE, "0.1,10,0.1,suffix:km"), 2.5);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/clouds/wind", PROPERTY_HINT_RANGE, "0,0.2,0.001,suffix:km/s"), 0.01);
	GLOBAL_DEF("rendering/fellow/post/clouds/interleaved", true); // decision 0115
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/clouds/shadows", PROPERTY_HINT_RANGE, "0,1,0.01"), 1.0); // decision 0133
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/clouds/sky_light", PROPERTY_HINT_RANGE, "0,1,0.01"), 1.0); // decision 0133
	GLOBAL_DEF("rendering/fellow/post/atmosphere/enabled", false); // decision 0108
	GLOBAL_DEF("rendering/fellow/post/atmosphere/aerial_perspective", true);
	GLOBAL_DEF("rendering/fellow/post/atmosphere/sun_transmittance", true); // the sun's light coloured by the atmosphere
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/atmosphere/aerial_distance_scale", PROPERTY_HINT_RANGE, "0,100,0.01"), 1.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/atmosphere/mie_scale", PROPERTY_HINT_RANGE, "0,20,0.01"), 1.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/atmosphere/rayleigh_scale", PROPERTY_HINT_RANGE, "0,10,0.01"), 1.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/atmosphere/ground_albedo", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.3);
	GLOBAL_DEF("rendering/fellow/post/atmosphere/srgb_rayleigh", true); // false: Unreal's coefficients
	GLOBAL_DEF("rendering/fellow/post/contact_shadows/enabled", false); // decision 0107
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/contact_shadows/length", PROPERTY_HINT_RANGE, "0.01,5,0.01,suffix:m"), 0.3);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/post/contact_shadows/steps", PROPERTY_HINT_RANGE, "2,64,1"), 8);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/contact_shadows/thickness", PROPERTY_HINT_RANGE, "0.001,1,0.001,suffix:m"), 0.02);
	GLOBAL_DEF("rendering/fellow/post/exposure/histogram", false); // decision 0105
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/exposure/low_percent", PROPERTY_HINT_RANGE, "0,100,0.5,suffix:%"), 10.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/exposure/high_percent", PROPERTY_HINT_RANGE, "0,100,0.5,suffix:%"), 90.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/exposure/speed_up", PROPERTY_HINT_RANGE, "0,20,0.1"), 3.0); // exposure rising (to darker scenes)
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/exposure/speed_down", PROPERTY_HINT_RANGE, "0,20,0.1"), 1.0);
	GLOBAL_DEF("rendering/fellow/post/local_exposure/enabled", false); // decision 0106
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/local_exposure/highlight_contrast", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.8);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/local_exposure/shadow_contrast", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.8);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/local_exposure/detail_strength", PROPERTY_HINT_RANGE, "0,4,0.01"), 1.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/local_exposure/blurred_blend", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.6);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/local_exposure/blur_size", PROPERTY_HINT_RANGE, "0.05,1,0.01"), 0.5);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/lens/chromatic_aberration", PROPERTY_HINT_RANGE, "-0.05,0.05,0.0005"), 0.0); // decision 0079
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/lens/vignette", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/lens/vignette_smoothness", PROPERTY_HINT_RANGE, "0.01,1,0.01"), 0.5);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/post/lens/film_grain", PROPERTY_HINT_RANGE, "0,0.2,0.001"), 0.0);
	// Ray tracing (decision 0080; start-up): mesh buffers usable as acceleration structure inputs, a scene of the
	// instances near the camera built for the ray-traced effects that are on.
	GLOBAL_DEF_RST("rendering/fellow/raytracing/enabled", false);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/raytracing/radius", PROPERTY_HINT_RANGE, "10,5000,1,suffix:m"), 200.0);
	GLOBAL_DEF("rendering/fellow/raytracing/skinned", true); // skinned and blend shape meshes rebuilt when they change
	GLOBAL_DEF("rendering/fellow/raytracing/multimesh", true); // MultiMesh instances in the scene (decision 0080)
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/raytracing/multimesh_min_angle", PROPERTY_HINT_RANGE, "0,10,0.05,suffix:°"), 1.0); // MultiMesh instances smaller than this seen from the camera left out (as Unreal's r.RayTracing.Culling.Angle)
	GLOBAL_DEF("rendering/fellow/raytracing/ambient_occlusion", false); // decision 0081: RTAO in place of SSAO
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/raytracing/ao_rays", PROPERTY_HINT_RANGE, "1,16,1"), 2);
	GLOBAL_DEF("rendering/fellow/raytracing/ao_interleaved", true); // a quarter of the pixels traced per frame, the others from history
	GLOBAL_DEF("rendering/fellow/raytracing/sun_shadows", false); // decision 0082: the first directional light's shadow traced
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/raytracing/sun_shadow_rays", PROPERTY_HINT_RANGE, "1,16,1"), 1); // per pixel and frame (soft shadows)
	GLOBAL_DEF("rendering/fellow/raytracing/sun_shadows_interleaved", true); // a quarter of the pixels traced per frame, the others from history clamped to the traced neighbours
	GLOBAL_DEF("rendering/fellow/raytracing/gi", false); // decision 0083: DDGI probes in place of the ambient light
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/raytracing/gi_probe_spacing", PROPERTY_HINT_RANGE, "0.25,16,0.05,suffix:m"), 2.0);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/raytracing/gi_probes_xz", PROPERTY_HINT_RANGE, "4,64,1"), 24);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/raytracing/gi_probes_y", PROPERTY_HINT_RANGE, "2,32,1"), 8);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/raytracing/gi_rays", PROPERTY_HINT_RANGE, "16,256,1"), 128);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/raytracing/gi_hysteresis", PROPERTY_HINT_RANGE, "0,0.999,0.001"), 0.97);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/raytracing/gi_energy", PROPERTY_HINT_RANGE, "0,4,0.01"), 1.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/raytracing/gi_sky_leak", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.1); // the share of the ambient light kept where the probes are dark (Lumen's "skylight leaking")
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/raytracing/gi_update_interval", PROPERTY_HINT_RANGE, "1,8,1"), 4); // frames between a probe's updates (a share of the probes traced per frame)
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/raytracing/gi_cascades", PROPERTY_HINT_RANGE, "1,2,1"), 2); // 2: a coarser volume (4 x the spacing, half the update rate) around the fine one, read where it ends
	GLOBAL_DEF("rendering/fellow/raytracing/gi_probe_relocation", true); // probes moved out of geometry and away from surfaces (within their cell)
	GLOBAL_DEF("rendering/fellow/raytracing/reflections", false); // decision 0084: traced reflections in place of SSR
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/raytracing/reflections_max_roughness", PROPERTY_HINT_RANGE, "0,1,0.01"), 0.7); // SSR's range (it fades out from 0.6 to 0.7)
	GLOBAL_DEF("rendering/fellow/raytracing/reflections_interleaved", true); // surfaces rougher than 0.3 traced a quarter of the pixels per frame
	GLOBAL_DEF("rendering/fellow/debug/raytracing_decode_positions", false);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/debug/raytracing_gi_dump_every", PROPERTY_HINT_RANGE, "0,10000,1"), 0); // DDGI probes written to user://fellow_ddgi_dump.bin every n updates (diagnostics; stalls the GPU)
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/debug/raytracing_gi_reference", PROPERTY_HINT_RANGE, "0,16,1"), 0); // n > 0: a path traced per pixel (n bounces, accumulated while the camera stands still) in place of the probes' irradiance: a reference for DDGI
	GLOBAL_DEF("rendering/fellow/debug/raytracing_gi_in_shader", false); // DDGI looked up by the opaque passes themselves (no screen pass): for comparisons
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/debug/raytracing_view", PROPERTY_HINT_ENUM, "Off,Hit Distance,Depth Match"), 0); // compressed positions decoded even where the device builds from them
	GLOBAL_DEF("rendering/fellow/animation/elide_modifier_backup", true); // decision 0076 (exact; on from fellow168)
	GLOBAL_DEF("rendering/fellow/animation/full_weight_slerp", true); // decision 0076 (exact; on from fellow168)
	GLOBAL_DEF("rendering/fellow/animation/track_shortcuts", true); // decision 0076 (exact; on from fellow168)
	GLOBAL_DEF("rendering/fellow/animation/key_hints", true); // decision 0076 (exact; from fellow170)
	GLOBAL_DEF("rendering/fellow/animation/batched_skin_transforms", true); // decision 0077 (exact; on from fellow170)
	GLOBAL_DEF("rendering/fellow/animation/shared_no_override_pose", true); // decision 0163 (exact)
	GLOBAL_DEF("rendering/fellow/animation/lazy_simulator_poses", true); // decision 0163
	GLOBAL_DEF("rendering/fellow/physics/distance_face_cull", true); // decision 0085 (exact)
	GLOBAL_DEF("rendering/fellow/physics/distance_face_order", true); // decision 0085 (exact)
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/physics/distance_face_batch", PROPERTY_HINT_RANGE, "1,32,1"), 8);
	GLOBAL_DEF("rendering/fellow/physics/convex_vertex_cache", true); // decision 0086 (exact)
	GLOBAL_DEF("rendering/fellow/physics/parallel_pair_culls", true); // decision 0087 (exact)
	GLOBAL_DEF("rendering/fellow/physics/pair_prefilter", true); // decision 0219 (exact)
	GLOBAL_DEF("rendering/fellow/streaming/texture_swaps_keep_shadows", true); // decision 0222 (exact)
	GLOBAL_DEF("rendering/fellow/streaming/texture_budget_from_used_level", true); // decision 0223 (no change without a budget)
	GLOBAL_DEF("rendering/fellow/frame/transfer_staging_trim", true); // decision 0225 (host memory; the same frames)
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/frame/transfer_staging_trim_idle_s", PROPERTY_HINT_RANGE, "0,60,0.1"), 5.0);
	GLOBAL_DEF("rendering/fellow/frame/transfer_large_own_submit", false); // decision 0227 (to be measured)
	GLOBAL_DEF("rendering/fellow/physics/jolt_deferred_sensor_contacts", true); // decision 0088
	GLOBAL_DEF("rendering/fellow/physics/parallel_island_solve", true); // decision 0089 (exact)
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/physics/parallel_island_solve_grab", PROPERTY_HINT_RANGE, "1,1024,1"), 16);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/physics/jolt_max_jobs", PROPERTY_HINT_RANGE, "-1,256,1"), -1); // decision 0090 (exact)
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/physics/jolt_bodies_per_job", PROPERTY_HINT_RANGE, "1,100000,1"), 128);
	GLOBAL_DEF("rendering/fellow/physics/island_constraint_lists", true); // decision 0091 (exact)
	GLOBAL_DEF("rendering/fellow/physics/jolt_job_pumps", false); // decision 0092 (exact; REJECT, no gain on the laptop)
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/physics/jolt_pump_spins", PROPERTY_HINT_RANGE, "1,1000000,1"), 2000);
	GLOBAL_DEF("rendering/fellow/physics/motion_query_cache", true); // decision 0093 (exact)
	GLOBAL_DEF("rendering/fellow/physics/distance_face_shape_axes", true); // decision 0096 (exact)
	GLOBAL_DEF("rendering/fellow/physics/distance_face_cull_cache", true); // decision 0097 (exact)
	GLOBAL_DEF("rendering/fellow/physics/ray_prune", true); // decision 0098 (exact)
	GLOBAL_DEF("rendering/fellow/physics/distance_face_sweep_reuse", true); // decision 0100 (exact)
	GLOBAL_DEF("rendering/fellow/physics/fast/jolt_shape_cast", false); // decision 0095 (fast mode: not stock's results)
	GLOBAL_DEF("rendering/fellow/physics/fast/godot_toi", false); // decision 0102 (fast mode: not stock's results)
	GLOBAL_DEF("rendering/fellow/physics/verify", false);
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
	GLOBAL_DEF("rendering/fellow/fluid/gas_rendering", true);
	GLOBAL_DEF("rendering/fellow/fluid/gas_scene_shadows", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/fluid/gas_solver", PROPERTY_HINT_ENUM, "Node's,SOR,Multigrid"), 0);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/fluid/gas_mg_tail_cells", PROPERTY_HINT_RANGE, "0,65536,1"), 0);
	// Milestone 61: the multigrid's red-black sweeps before and after the finest level's correction (2: decision 0117's).
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/fluid/gas_mg_fine_sweeps", PROPERTY_HINT_RANGE, "1,4,1"), 2);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/fluid/gas_sparse", PROPERTY_HINT_ENUM, "Node's,Off,On"), 0);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/fluid/gas_sparse_margin", PROPERTY_HINT_RANGE, "0,4,1"), 0);
	GLOBAL_DEF("rendering/fellow/fluid/gas_profile_passes", false);
	// fluid/burn_vertices_cached (decision 0120): a burning mesh's vertices march into the voxels once per frame with
	// steps, in a compute pass writing a texture its materials read by VERTEX_ID, instead of in every pass that draws
	// them (the depth prepass, the opaque pass, each shadow). Off: the march in the material's vertex code, as before.
	// Read when a FellowBurnable3D builds its display (fellow_burnable_3d.cpp).
	GLOBAL_DEF("rendering/fellow/fluid/burn_vertices_cached", true);
	GLOBAL_DEF("rendering/fellow/profiling/pass_timestamps", false);
	GLOBAL_DEF("rendering/fellow/profiling/list_markers", false);
	GLOBAL_DEF("rendering/fellow/shadows/enabled", true);
	GLOBAL_DEF("rendering/fellow/shadows/directional_cache", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_guard_texels", PROPERTY_HINT_RANGE, "16,1024,16"), 256);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_static_frames", PROPERTY_HINT_RANGE, "1,600,1"), 60);
	// Decision 0125: casters moved only by the wind are cached from this directional cascade on (their far shadows keep
	// the pose of the cache's last build); -1: never.
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/wind_cached_from_cascade", PROPERTY_HINT_RANGE, "-1,3,1"), 2);
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
	// Shadow caster contribution culling (decision 0121): casters whose bounding sphere spans fewer than this many texels
	// of a directional shadow cascade are not drawn into it (0: off).
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/shadows/contribution_cull_texels", PROPERTY_HINT_RANGE, "0,64,0.1"), 0.0);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/shadows/directional_cache_debug_mode", PROPERTY_HINT_ENUM, "Normal,Lattice Only (No Cache),Rebuild Every Frame"), 0);
	GLOBAL_DEF("rendering/fellow/geometry/enabled", true);
	GLOBAL_DEF("rendering/fellow/geometry/mesh_clusters", true);
	GLOBAL_DEF("rendering/fellow/geometry/cluster_blended_surfaces", true);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/geometry/cluster_triangles", PROPERTY_HINT_RANGE, "64,65536,1"), 4096);
	GLOBAL_DEF("rendering/fellow/frame/pipeline_fallback", false); // decision 0059 (rejected, off)
	GLOBAL_DEF("rendering/fellow/frame/depth_pipeline_keys", true); // decision 0069
	GLOBAL_DEF("rendering/fellow/frame/clear_new_textures", true); // decision 0070
	GLOBAL_DEF("rendering/fellow/frame/movie_wait_pipelines", true); // decision 0072
	GLOBAL_DEF("rendering/fellow/frame/stable_scene_bits", true); // decision 0161
	// Recorded pipelines (decision 0171): read once, when the renderer starts.
	GLOBAL_DEF_RST("rendering/fellow/pipelines/record", false);
	GLOBAL_DEF_RST("rendering/fellow/pipelines/replay", true);
	GLOBAL_DEF_RST(PropertyInfo(Variant::STRING, "rendering/fellow/pipelines/path", PROPERTY_HINT_FILE, "*.json"), "res://fellow_pipelines.json");
	// Milestone 61: FellowEngine nodes' compute kernels (water, rain, shallow water) kept as SPIR-V in
	// user://fellow_kernel_cache after their first compilation: glslang runs once per machine and build, not per session.
	GLOBAL_DEF("rendering/fellow/frame/kernel_spirv_cache", true);
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
	GLOBAL_DEF("rendering/fellow/postfx/screen_copy_mipmaps_on_demand", true); // decision 0123: exact
	GLOBAL_DEF("rendering/fellow/postfx/depth_copy_nearest_mipmaps", true); // decision 0126
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
	// Contribution culling (decision 0121): nodes and MultiMesh instances whose bounding sphere covers fewer than this many
	// pixels of the viewport's width are not drawn by the camera (0: off).
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/geometry/contribution_cull_pixels", PROPERTY_HINT_RANGE, "0,64,0.1,suffix:px"), 0.0);
	// MultiMesh instances below the threshold: culled, or thinned (a share kept and grown so their coverage stays).
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/geometry/contribution_cull_instances", PROPERTY_HINT_ENUM, "Cull,Thin"), 1);
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
	GLOBAL_DEF("rendering/fellow/frame/lazy_shader_variants_fill", true); // decision 0176
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
	// FSR 3.1 upscaler (milestone 47, decision 0148, renderer_rd/effects/fsr3.h): a viewport in the FSR 2 scaling mode runs
	// AMD's FidelityFX Super Resolution 3.1 upscaler instead of stock's 2.2. Read every frame (a new context, a new history).
	// On by default from milestone 49: on the RTX it costs what FSR 2.2 costs (within 1 %), and it is better in motion.
	GLOBAL_DEF("rendering/fellow/upscaling/fsr3", true);
	// Half precision in the passes that have it (where the GPU supports it; not in the accumulation on NVIDIA).
	GLOBAL_DEF_RST("rendering/fellow/upscaling/fsr3_half_precision", true);
	// The SDK's tunables (ffxFsr3UpscalerSetConstant): 0 velocity factor steadies bright pixels; a larger reactiveness or
	// shading change scale reacts faster; less accumulation added a frame ghosts less where the history is rejected;
	// the accumulation a disoccluded pixel starts from.
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/upscaling/fsr3_velocity_factor", PROPERTY_HINT_RANGE, "0,1,0.01"), 1.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/upscaling/fsr3_reactiveness_scale", PROPERTY_HINT_RANGE, "0,10,0.01"), 1.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/upscaling/fsr3_shading_change_scale", PROPERTY_HINT_RANGE, "0,10,0.01"), 1.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/upscaling/fsr3_accumulation_added_per_frame", PROPERTY_HINT_RANGE, "0,1,0.001"), 1.0 / 3.0);
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/upscaling/fsr3_min_disocclusion_accumulation", PROPERTY_HINT_RANGE, "-1,1,0.001"), -1.0 / 3.0);
	// The SDK's debug view over the output (dilated motion vectors, depth, reactive masks, the history).
	GLOBAL_DEF("rendering/fellow/upscaling/fsr3_debug_view", false);

	// Texture streaming (milestone 45, servers/rendering/fellow_texture_streaming.h). Off: textures load whole, as stock.
	GLOBAL_DEF_RST("rendering/fellow/streaming/textures", false);
	// Mips at or below this size (the larger side) are always resident; smaller textures are not streamed.
	GLOBAL_DEF_RST(PropertyInfo(Variant::INT, "rendering/fellow/streaming/texture_resident_size", PROPERTY_HINT_RANGE, "16,4096,1,suffix:px"), 128);
	// The streamed textures' memory (0: no limit); over it every streamed texture is coarsened by the same bias.
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/streaming/texture_budget_mb", PROPERTY_HINT_RANGE, "0,65536,1,suffix:MiB"), 0);
	// Added to the estimated level (negative: finer, a margin; positive: coarser). -1: the estimate is the finest level
	// along the UV mapping's largest stretch; a GPU's level, the larger of the screen axes' derivatives, can be half a
	// level finer when that stretch is diagonal on screen, and half a level more where the derivatives' lengths are
	// approximated (Vulkan allows it). At -0.5 lavapipe showed 1-level differences on 0.3 % of one view; at -1 none.
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/streaming/texture_lod_bias", PROPERTY_HINT_RANGE, "-4,4,0.05"), -1.0);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/streaming/texture_instances_per_frame", PROPERTY_HINT_RANGE, "64,1000000,1"), 2048);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/streaming/texture_max_jobs", PROPERTY_HINT_RANGE, "1,64,1"), 4);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/streaming/texture_upload_mb_per_frame", PROPERTY_HINT_RANGE, "1,4096,1,suffix:MiB"), 64);
	// A texture needing fewer mips keeps them this long before they are released (unless over the budget).
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/streaming/texture_release_hold_s", PROPERTY_HINT_RANGE, "0,600,0.1,suffix:s"), 2.0);
	// A streamed texture no instance uses any more keeps its mips this long, then falls back to its resident tail.
	GLOBAL_DEF(PropertyInfo(Variant::FLOAT, "rendering/fellow/streaming/texture_unreferenced_hold_s", PROPERTY_HINT_RANGE, "0,600,0.1,suffix:s"), 5.0);
	// RenderingServer.fellow_get_texture_streaming_info() also lists every streamed texture (path, levels, bytes).
	GLOBAL_DEF("rendering/fellow/streaming/texture_info_details", false);
	// FellowTerrain3D's edits upload the texels they changed (and the mips over them) instead of whole textures.
	GLOBAL_DEF("rendering/fellow/streaming/terrain_region_uploads", true);
	// FellowTerrain3D's virtual texture (milestone 70): the atlas every terrain's pages share (texels along a side: two
	// RGBA8 textures), and the pages baked a frame across the terrains.
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/terrain/virtual_texture_atlas_size", PROPERTY_HINT_RANGE, "544,16320,136,suffix:texels"), 4080);
	GLOBAL_DEF(PropertyInfo(Variant::INT, "rendering/fellow/terrain/virtual_texture_pages_per_frame", PROPERTY_HINT_RANGE, "1,1024,1"), 16);
	// Milestone 71: every FellowTerrain3D drawn from a virtual texture, as if its virtual_texture were on (comparing a project
	// with and without them; read when a terrain enters the tree).
	GLOBAL_DEF("rendering/fellow/terrain/virtual_texture_all", false);
}

// Hot workers' gate (render thread only): exponential averages of the fence wait and the frame interval; the gate
// closes when the waits exceed 10 % of the frame and opens again below 3 % (hysteresis: a frame that just turned
// CPU-bound by going hot does not flip back and forth).
static double fellow_gate_wait_avg = 0.0;
static double fellow_gate_frame_avg = 0.0;
static std::atomic<bool> fellow_gate_open{ true };

// Milestone 113 (decision 0222): the streamed texture swap's flags (render thread).
bool &Fellow::texture_stream_swap_active() {
	static thread_local bool active = false;
	return active;
}

bool &Fellow::material_change_is_texture_swap() {
	static thread_local bool swap = false;
	return swap;
}

bool Fellow::raytracing_active() {
	// Decided once: the mesh buffers created from then on carry the acceleration structure input usage or not.
	static std::atomic<int> state{ -1 };
	int s = state.load(std::memory_order_acquire);
	if (s >= 0) {
		return s == 1;
	}
	RenderingDevice *rd = RenderingDevice::get_singleton();
	if (rd == nullptr) {
		return false; // not decided before the device exists
	}
	const bool requested = raytracing_requested();
	const bool supported = rd->has_feature(RD::SUPPORTS_RAY_QUERY) && rd->has_feature(RD::SUPPORTS_BUFFER_DEVICE_ADDRESS);
	if (requested && !supported) {
		WARN_PRINT("FellowEngine: rendering/fellow/raytracing/enabled is set, but this device has no ray query support; ray-traced effects are off.");
	}
	s = (requested && supported) ? 1 : 0;
	state.store(s, std::memory_order_release);
	if (s == 1) {
		print_verbose("FellowEngine: ray tracing on (ray queries).");
	}
	return s == 1;
}

static std::atomic<bool> fellow_editor_animate{ true };

void Fellow::set_editor_animate(bool p_animate) {
	fellow_editor_animate.store(p_animate, std::memory_order_relaxed);
}

bool Fellow::is_editor_animate() {
	return fellow_editor_animate.load(std::memory_order_relaxed);
}

void Fellow::request_redraw() {
	RenderingServerDefault::redraw_request();
}

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

bool Fellow::frame_stable_scene_bits() {
	return enabled() && GLOBAL_GET_CACHED(bool, "rendering/fellow/frame/stable_scene_bits");
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
	d["physics_ticks"] = s.physics_ticks.get();
	d["physics_sync_usec"] = s.physics_sync_usec.get();
	d["physics_process_usec"] = s.physics_process_usec.get();
	d["physics_step_usec"] = s.physics_step_usec.get();
	d["physics_end_usec"] = s.physics_end_usec.get();
	d["main_frames"] = s.main_frames.get();
	d["main_process_usec"] = s.main_process_usec.get();
	d["main_draw_usec"] = s.main_draw_usec.get();
	d["main_sync_usec"] = s.main_sync_usec.get();
	d["rs_queue_flush_usec"] = s.rs_queue_flush_usec.get();
	d["rs_queue_flush_commands"] = s.rs_queue_flush_commands.get();
	d["draw_begin_frame_usec"] = s.draw_begin_frame_usec.get();
	d["draw_setup_usec"] = s.draw_setup_usec.get();
	d["draw_particles_probes_usec"] = s.draw_particles_probes_usec.get();
	d["draw_viewports_usec"] = s.draw_viewports_usec.get();
	d["draw_end_frame_usec"] = s.draw_end_frame_usec.get();
	d["draw_post_usec"] = s.draw_post_usec.get();
	d["pipeline_cache_bytes"] = s.pipeline_cache_bytes.get();
	{
		static const char *godot_phases[6] = { "integrate_forces", "broadphase", "islands", "setup", "solve", "integrate_velocities" };
		for (int i = 0; i < 6; i++) {
			d[String("physics_godot_") + godot_phases[i] + "_usec"] = s.physics_godot_usec[i].get();
		}
		static const char *bp_phases[3] = { "bp_optimize", "bp_cull", "bp_pairs" };
		for (int i = 0; i < 3; i++) {
			d[String("physics_godot_") + bp_phases[i] + "_usec"] = s.physics_godot_bp_usec[i].get();
		}
		static const char *jolt_phases[3] = { "pre_step", "update", "post_step" };
		for (int i = 0; i < 3; i++) {
			d[String("physics_jolt_") + jolt_phases[i] + "_usec"] = s.physics_jolt_usec[i].get();
		}
	}
	if (RenderingDevice *rd = RenderingDevice::get_singleton()) {
		// Video memory as the driver counts it (every heap) split: the device's own heaps, and of the host's the transfer
		// workers' staging buffers (kept at the largest upload's power of two, milestone 113).
		uint32_t workers = 0;
		d["memory_device_local_bytes"] = rd->fellow_device_local_memory_used();
		d["memory_total_bytes"] = rd->get_memory_usage(RenderingDevice::MEMORY_TOTAL);
		d["transfer_staging_bytes"] = rd->fellow_transfer_staging_bytes(&workers);
		d["transfer_workers"] = workers;
	}
	d["transfer_staging_grows"] = s.transfer_staging_grows.get();
	d["transfer_staging_trims"] = s.transfer_staging_trims.get();
	d["transfer_submit_waits"] = s.transfer_submit_waits.get();
	d["transfer_submit_wait_usec"] = s.transfer_submit_wait_usec.get();
	d["transfer_submit_skips"] = s.transfer_submit_skips.get();
	d["transfer_own_submits"] = s.transfer_own_submits.get();
	d["physics_godot_pairs_made"] = s.physics_godot_pairs_made.get();
	d["physics_godot_pairs_freed"] = s.physics_godot_pairs_freed.get();
	d["physics_motion_cache_calls"] = s.physics_motion_cache_calls.get();
	d["physics_motion_cache_hits"] = s.physics_motion_cache_hits.get();
	d["physics_verify_faces"] = s.physics_verify_faces.get();
	d["physics_verify_mismatches"] = s.physics_verify_mismatches.get();
	d["raytracing_instances"] = s.raytracing_instances.get();
	d["raytracing_multimesh_instances"] = s.raytracing_multimesh_instances.get();
	d["raytracing_blas_builds"] = s.raytracing_blas_builds.get();
	d["raytracing_albedo_textures"] = s.raytracing_albedo_textures.get();
	d["raytracing_build_usec"] = s.raytracing_build_usec.get();
	d["raytracing_tlas_usec"] = s.raytracing_tlas_usec.get();
	d["geometry_cluster_surfaces_dropped"] = s.geometry_cluster_surfaces_dropped.get();
	d["geometry_cluster_blended_after_activation"] = s.geometry_cluster_blended_after_activation.get();
	d["geometry_cluster_build_usec"] = s.geometry_cluster_build_usec.get();
	d["geometry_cluster_draws"] = s.geometry_cluster_draws.get();
	d["pipeline_fallback_draws"] = s.pipeline_fallback_draws.get();
	d["pipeline_ubershader_draws"] = s.pipeline_ubershader_draws.get();
	d["pipelines_recorded"] = s.pipelines_recorded.get();
	d["pipelines_replayed"] = s.pipelines_replayed.get();
	d["cost_shader_start_usec"] = s.cost_shader_start_usec.get();
	d["cost_shader_starts"] = s.cost_shader_starts.get();
	d["cost_shader_wait_usec"] = s.cost_shader_wait_usec.get();
	d["cost_shader_waits"] = s.cost_shader_waits.get();
	d["cost_shader_variant_usec"] = s.cost_shader_variant_usec.get();
	d["cost_shader_variants"] = s.cost_shader_variants.get();
	d["cost_shader_cache_loads"] = s.cost_shader_cache_loads.get();
	d["cost_shader_lazy_creates"] = s.cost_shader_lazy_creates.get();
	d["cost_shader_background_creates"] = s.cost_shader_background_creates.get();
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
	d["cost_glsl_compile_usec"] = s.cost_glsl_compile_usec.get();
	d["cost_glsl_compiles"] = s.cost_glsl_compiles.get();
	d["cost_glsl_cached_usec"] = s.cost_glsl_cached_usec.get();
	d["cost_glsl_cached"] = s.cost_glsl_cached.get();
	d["cost_texture_create_usec"] = s.cost_texture_create_usec.get();
	d["cost_texture_creates"] = s.cost_texture_creates.get();
	d["cost_buffer_init_usec"] = s.cost_buffer_init_usec.get();
	d["cost_buffer_inits"] = s.cost_buffer_inits.get();
	d["cost_buffer_init_bytes"] = s.cost_buffer_init_bytes.get();
	d["cost_mesh_surface_usec"] = s.cost_mesh_surface_usec.get();
	d["cost_mesh_surfaces"] = s.cost_mesh_surfaces.get();
	d["cost_multimesh_buffer_usec"] = s.cost_multimesh_buffer_usec.get();
	d["cost_multimesh_buffers"] = s.cost_multimesh_buffers.get();
	d["terrain_vt_pages_baked"] = s.terrain_vt_pages_baked.get();
	d["terrain_vt_dispatch_usec"] = s.terrain_vt_dispatch_usec.get();
	d["terrain_vt_update_usec"] = s.terrain_vt_update_usec.get();
	d["terrain_vt_setup_usec"] = s.terrain_vt_setup_usec.get();
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
	d["geometry_multimesh_instances_small"] = s.geometry_multimesh_instances_small.get();
	d["geometry_contribution_culled"] = s.geometry_contribution_culled.get();
	d["shadow_contribution_culled"] = s.shadow_contribution_culled.get();
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
	d["rd_free_pending_usec"] = s.rd_free_pending_usec.get();
	d["rd_free_pendings"] = s.rd_free_pendings.get();
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
