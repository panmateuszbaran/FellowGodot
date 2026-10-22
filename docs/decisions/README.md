# FellowEngine decision records

Every change to engine behaviour or performance goes through a decision gate before it can be enabled by default.
One file per experiment, numbered in the order they were started. A record is written when the experiment is
measured, not when it is proposed, and it is never rewritten to hide a result: later re-measurements are appended.

## Gate rules

* The change must be isolated behind a runtime toggle under `rendering/fellow/*` (default **off** until KEEP), so
  stock behaviour and the experiment run from the same binary.
* Evidence must come from the benchmark suite (`benchmarks/`), interleaved ABBA runs on the same machine, with the
  settings recorded next to the results. Anything measured on another machine or driver is labelled as such.
* Performance and quality are judged together. A speed-up that changes the image must say how (diff images, PSNR/
  SSIM/LPIPS vs. the accumulated ground truth, temporal metrics); an image change that costs time must say how much.
* Anti-cheating: no lowered internal resolution, sample counts, shadow resolution, LOD, draw distance or effect
  quality unless that *is* the experiment and it is reported as a quality trade-off (iso-performance comparison).
* Decisions: **KEEP** (may be enabled by default), **REWORK** (promising, not acceptable as is), **REJECT**
  (removed or left off; the record stays).

## Template

```
# NNNN — <title>

Status: KEEP | REWORK | REJECT | MEASURING      Branch / commit: ...      Toggle: rendering/fellow/...

## Problem
## Evidence (before the change)
## Stock implementation
## Proposed implementation
## Expected benefit
## Actual performance        (tables: scene, resolution, stock vs fellow, delta, CV, runs)
## Actual visual quality     (captures, diffs, metrics vs. ground truth, temporal metrics)
## Regression risk
## Decision
```

## Index

| # | Experiment | Toggle | Decision |
|---|---|---|---|
| [0001](0001-taa-resolve-v2.md) | TAA resolve v2 (reverse-Z dilation, YCoCg variance clip, no clip collapse) | `postfx/taa_resolve_v2` | see record |
| [0002](0002-taa-history-pingpong.md) | TAA history ping-pong (no history/velocity copies) | `postfx/taa_history_pingpong` | see record |
| [0003](0003-gpu-shallow-water.md) | GPU shallow-water prototype (`FellowShallowWater3D`) | `fluid/enabled` (new feature) | see record |
| [0004](0004-directional-shadow-cache.md) | Directional shadow static cache (guard-banded per-cascade cache, depth re-based copy, dynamic casters on top) | `shadows/directional_cache` | KEEP |
| [0005](0005-directional-shadow-sampling.md) | Directional/positional shadow sampling: N·L skips, unblended cascade skip (exact); PCSS blend band, parallel-light penumbra (look); min/max window pyramid early-outs (exact) | `shadows/skip_*`, `shadows/pcss_*`, `shadows/directional_max_mip`, `shadows/positional_max_mip` | skips KEEP; look changes optional; pyramid v1 REJECT, v7 KEEP |
| 0006 (branch `fellow/ssao`) | Temporal SSAO (rotated pattern + accumulation) | `ssao/*` | REJECT / parked |
| [0007](0007-mesh-clusters.md) | Mesh clusters: frustum culling of k-d clusters inside large static surfaces | `geometry/mesh_clusters` | KEEP |
| [0008](0008-light-cluster-depth.md) | Light clusters with logarithmic depth slices and conservative per-element depth ranges; zero-attenuation light skip | `lights/cluster_depth_slices`, `lights/zero_attenuation_skip` | KEEP |
| [0009](0009-early-submit.md) | Frame/GPU gap: early submit, present from a second queue | `frame/early_submit`, `frame/separate_present_queue` | REJECT |
| [0010](0010-blended-alpha-zero-skip.md) | Mix-blended quads with alpha 0 skip their lighting | `shading/blended_alpha_zero_skip` | KEEP |
| [0011](0011-shared-memory-tiles.md) | SSAO gather and TAA v2 neighbourhoods loaded once per workgroup (shared memory) | `postfx/ssao_shared_tile` | REJECT (no measurable effect) |
| [0012](0012-specialization-constants.md) | FellowEngine shader features as specialization constants (no code for disabled features) | (each feature's toggle) | KEEP |
| [0013](0013-taa-camera-motion-vectors.md) | TAA motion vectors of static geometry from the depth buffer (upscalers' motion-pass path) | `postfx/taa_camera_motion_vectors` | REJECT (within noise, not bit-identical) |
| [0014](0014-cluster-pack-per-tile.md) | Cluster pack pass with one workgroup per tile | `lights/cluster_pack_parallel` | KEEP |
| [0015](0015-async-compute.md) | Async compute: SSAO/SSIL/SSR on a second queue, concurrent with shadows, clusters and fog | `frame/async_compute` (start-up) | KEEP, on by default on NVIDIA GPUs from `fellow71` (autotune decides per scene) |
| [0016](0016-parallel-render-lists.md) | Render lists (fill, sort, instance data), the shadow passes' lists and the scene cull built by the render thread and worker tasks (hot workers), exact | `cpu/parallel_list_fill`, `cpu/parallel_instance_data`, `cpu/compact_list_sort`, `cpu/parallel_list_sort`, `cpu/parallel_cull`, `cpu/batched_shadow_lists`, `cpu/compact_shadow_instances`, `cpu/hot_workers_usec` | KEEP, on by default since `fellow56` (City Lab −19 % frame, −31 % render thread vs stock) |
| [0017](0017-frame-replay-present.md) | End of the frame: draw lists replayed into secondary command buffers by worker threads (CPU-bound frames); presentation and submissions on their own thread | `frame/parallel_replay`, `frame/present_thread`, `frame/submit_thread` | KEEP, on by default since `fellow56` |
| [0018](0018-parallel-recording.md) | Large draw lists recorded by several threads (forked into segments, joined in order), replayed in their recorded segments | `cpu/parallel_recording` | KEEP, on by default since `fellow56` (draw-heavy City Lab −29 % frame, −40 % render thread vs stock) |
| [0019](0019-frame-replay-plan.md) | Frame plan: the frame's draw lists split before the primary command buffer is recorded and replayed by the workers at once | `frame/replay_frame_plan` | KEEP as an option (off; draw-heavy City Lab −2.7 % in one process, nothing between processes) |
| [0020](0020-autotune.md) | Autotune: exact, scene-dependent optimizations (shadow pyramids, async compute, frame plan) switched on and off in alternating blocks of frames and kept the faster way, checked again when the frame time changes | `autotune/enabled`, `autotune/*` | KEEP, on by default from `fellow71` (with milestone 5 defaults: City Lab −4.1 %, draw-heavy −4.6 %, PBR −4.0 %, Lights −1.5 %) |
| [0021](0021-instance-buffer-memory.md) | Per-frame instance data buffers in system memory instead of device-local memory written over PCIe | `cpu/instance_buffer_memory` | REJECT (option, off: render thread −4 to −7 %, GPU +2.5 to +3.4 %; frame −1.9 % draw-heavy, up to +1.4 % GPU-bound) |
| [0022](0022-persistent-instance-data.md) | Persistent instance data: one entry per surface kept in device memory and rewritten only when its instance changed; 16-byte list elements per frame | `cpu/persistent_instances` | KEEP, on by default from `fellow71` (v3; an autotune tunable; render thread −4 to −14 %) |
| [0023](0023-taa-tile16.md) | TAA resolve in 16 × 16 workgroups, every thread loading the shared tile | `postfx/taa_tile16` | REJECT (option, off: TAA pass +0.006 to +0.021 ms) |
| [0024](0024-coherent-gather.md) | SSAO and SSIL gathers with one sample rotation per workgroup (threads remapped, every pixel computed as before) | `postfx/ss_coherent_gather` | REJECT (option, off: SSAO gather +0.016 to +0.066 ms) |
| [0025](0025-luminance-reduce.md) | Auto exposure: the luminance reduction with two barriers per workgroup instead of seven (the same additions) | `postfx/fast_luminance_reduce` | KEEP, on by default from `fellow74` (auto exposure pass −43 %, frame −0.4 to −0.8 %) |
| [0026](0026-occlusion-culling.md) | Occlusion culling against a depth pyramid (step 1: the opaque colour pass, exact; GPU-tested runs drawn indirectly) | `geometry/occlusion_culling` | REJECT (option, off: frame +0.1 to +5.6 %, Bistro exterior −1.0 %) |
| [0027](0027-adaptive-shading-rate.md) | Adaptive shading rate from the previous frame's image (visually lossless) | `shading/adaptive_vrs` | KEEP as an option (off; autotune keeps it only where measured faster: Lights Lab −21 %, PBR Lab −5 %, Bistro interior −3 %) |
| [0028](0028-temporal-ssao.md) | Temporal SSAO: half the taps per frame, the other half from the previous frame's history (visually lossless) | `postfx/ssao_temporal` | KEEP as an option (off; 57.5-82.3 dB, −1.5 to −3.1 % where SSAO is on the critical path) |
| [0029](0029-prepass-order.md) | Depth pre-pass with the shared-depth-material elements first | `geometry/prepass_simple_first` | REJECT (option, off: no change in the pre-pass, render thread +2.5 % in Bistro exterior) |
| [0030](0030-temporal-ssil.md) | Temporal SSIL: half the taps per frame (as 0028; also half-size SSAO/SSIL) | `postfx/ssil_temporal` | KEEP as an option (off; 52.1-62.9 dB, −1.0 to −1.5 %) |
| [0031](0031-fog-half-rate.md) | Volumetric fog: half the froxels lit per frame (checkerboard), the others reprojected | `postfx/fog_half_rate` | KEEP as an option (off; 56.5-72.2 dB, Lights Lab −1.7 %) |
| [0032](0032-shadow-temporal-taps.md) | Soft shadow filters with half their taps per frame under TAA | `shadows/temporal_taps` | REJECT (not merged: PBR Lab thin penumbrae 47 dB, max \|d\| 75) |
| [0033](0033-taa-direct-output.md) | TAA direct output: post-processing reads the resolve's output (one full-resolution copy less) | `postfx/taa_direct_output` | KEEP, on by default from `fellow91` (same images, frame −0.5 to −1.2 %) |
| [0034](0034-multimesh-culling.md) | MultiMesh instances culled one by one on the GPU per pass (camera, shadows) and drawn indirectly from a compacted buffer | `geometry/multimesh_culling` | KEEP, on by default from `fellow93` (exact; Foliage Lab one MultiMesh per type −52.9 % against stock, chunks −9.5 %) |
| [0035](0035-multimesh-instance-lod.md) | Per-instance mesh LOD for culled MultiMeshes (stock's rule per instance instead of per node) | `geometry/multimesh_instance_lod` | KEEP as an option (off; changes the image: aerial views 33-40 dB; −2.2 % with LOD meshes) |
| [0036](0036-multimesh-cull-vertex-motion.md) | MultiMesh culling for materials that move vertices (wind), the node's extra cull margin taken as the displacement's bound | `geometry/multimesh_cull_vertex_motion` | KEEP as an option (off; identical within the margin; Foliage Lab with wind −41.4 %) |
| [0037](0037-scene-shader-register-pressure.md) | Scene shader register pressure: shadow filter loops not unrolled / unrolled, directional soft shadow cascades in one loop | `debug/scene_shader_experiments` | REJECT (80 registers unchanged; the cascade loop −18 % binary, frame within noise) |
| [0038](0038-cluster-tile-size.md) | Light cluster tiles of 16 or 8 pixels instead of 32 | `debug/cluster_size` | REJECT (Lights Lab +2.3 % / +40 %, not exact in Stress Lab) |
| [0039](0039-pcss-filter-early-out.md) | Soft shadow filters: path shares; directional PCSS decided from the pyramid after the blocker search | `shadows/pcss_filter_early_out` | REJECT (exact with a 2^-15 margin; opaque pass ±0.02 ms) |
| [0040](0040-batched-shadow-taps.md) | Soft shadow filter taps (PCF, PCSS search and filter; directional and positional) fetched four at a time, results added in stock's order | `shadows/batched_taps` | KEEP, on by default from `fellow104` (exact; frame −2.1 to −5.0 % in one process) |
| [0041](0041-ss-tap-batch.md) | SSAO/SSIL gather taps fetched a few at a time, sums in stock's order | `postfx/ss_tap_batch` | REJECT (option, off: exact, frame within noise, Lights Lab +1.3 %; the gathers' 40-53 registers already hide the latency) |
| [0042](0042-pcss-quad-search.md) | Directional PCSS after 0040: a blended pixel's two cascades fetched together; the blocker search shared by 2 × 2 pixel quads | `debug/scene_shader_experiments` 2048, `shadows/pcss_quad_search` | REJECT (pair: slower; quad search: Foliage Lab 50.8 dB, 0.9 % of pixels > 2, for −0.2 to −2.0 %) |
| [0043](0043-light-loop-prefilter.md) | Positional light loop: sizing; the lights that cannot reach a pixel dropped first, four at a time | `debug/scene_shader_experiments` 4096, 8192, 32768 | REJECT (exact, opaque pass +0.01 to +0.1 ms; the lights are 61-76 % of the opaque pass in Lights Lab and Bistro interior) |
| [0044](0044-taa-velocity-and-last-frame.md) | TAA v2 velocities in shared memory; SSIL's last-frame copy without its unread full-size level | `postfx/taa_shared_velocity`, `postfx/ss_last_frame_direct` | KEEP, on by default from `fellow110` (exact; TAA pass −0.006 to −0.033 ms; frame −0.5 to −1.0 % where SSIL is on) |
| [0045](0045-light-fields-early.md) | Positional lights: where their time goes; their fields read together at the start of the light's function | `lights/fields_early` | KEEP, on by default (v2 from `fellow117`) in frames with omni/spot lights (exact; Bistro interior −3.1 %, Lights Lab −3.0 %) |
| [0046](0046-light-prefetch-next.md) | Light loop: the next light's fields (and the next cluster word) read while the current light is processed | `lights/prefetch_next` | REJECT (exact; 124-127 registers instead of 96: Lights Lab +5.6 to +10.7 %, Bistro interior +2.0 to +4.7 %) |
| [0047](0047-light-hot-fields-buffer.md) | Light loop's fields from an 80-byte copy in a storage or uniform buffer | `lights/hot_fields_buffer` | REJECT (exact; 114-117 registers: storage +1.4 to +2.4 %, uniform neutral) |
| [0048](0048-scene-shader-register-budget.md) | The colour pass's register budget: 96 registers with spills, or 114-128 without; positional PCF in one batch of 8; shadow fields after the range test | `debug/scene_shader_experiments` 1048576, 2097152 | finding; both experiments REJECT |
| [0049](0049-light-tile-cull.md) | Light tile culling: per 8 × 8 tile, after the depth pre-pass, the omni/spot lights that can reach its geometry; the opaque pass's light loops visit only those | `lights/tile_cull` (autotune) | KEEP, on by default from `fellow127` under autotune (exact; Lights Lab −5.6 %, elsewhere left off by autotune) |
| [0050](0050-pipeline-cache-limit.md) | The pipeline cache file: ignored above a size limit; not saved past 2 GiB (stock's checksum overflowed and crashed) | `frame/pipeline_cache_max_mb` | KEEP (robustness) |
| [0051](0051-cluster-build-v2.md) | The light cluster build: instanced proxy draws, the pack pass one invocation per element, the 21.5 MB render buffer left cleared by it instead of cleared every frame | `lights/cluster_pack_elements`, `lights/cluster_instanced_draws` | KEEP, on by default from `fellow130` (exact, `debug/cluster_verify` 0 mismatches on RTX; GPU −2.3 % Lights Lab, −1.1 % PBR Lab, −0.9 % City Lab) |
| [0052](0052-glow-blur-layout.md) | Glow's blur: an 8-way shared-memory bank conflict in every access of its vertical pass; 16 × 16 tiles (2.25 samples per output instead of 4) | `postfx/glow_blur` | KEEP, 16 × 16 tiles by default from `fellow130` (exact; glow 0.14-0.20 → 0.05-0.08 ms, GPU −1.1 to −4.5 % in every scene) |
| [0053](0053-fog-light-loop.md) | Volumetric fog's density pass: the light loops walked by the subgroup (the union of the warp's cluster masks); 2 × 2 × 16 workgroups | `postfx/fog_light_loop` | REJECT (lavapipe exact, RTX Lights Lab 90.9 dB against its 91.8 dB floor; the pass −5 % in Lights Lab, the frame +0.2 to +0.5 %) |
| [0054](0054-segment-barriers.md) | Render graph: barriers for a resource written in an earlier command buffer of the frame and used again with the same usage | `debug/segment_barriers` | KEEP, on by default from `fellow134` (synchronization; no measurable cost) |
| [0055](0055-tonemap-specialized.md) | Tone mapping: the frame's paths (FXAA, glow levels and mode, tone mapper, colour correction, debanding, ...) as specialization constants | `postfx/tonemap_specialized` | KEEP, on by default from `fellow138` (exact; the pass −12 to −14 %, GPU −0.3 to −1.1 %) |
| [0056](0056-fog-specialized.md) | Volumetric fog's density pass without the area light loop, VoxelGI and directional loops when the frame has none | `postfx/fog_specialized` | KEEP, on by default from `fellow138` (exact; the pass −21 to −27 %, Lights Lab GPU −0.9 %) |
| [0057](0057-absent-features-specialized.md) | The colour passes compiled without the decal, probe, directional, omni and spot code and the positional shadow lookups the frame's light cluster and light counts say cannot run; the values those blocks could change pass an identity the compiler cannot see through | `shading/absent_features_specialized` | KEEP, all six on by default from `fellow142` (exact; registers 96 → 72-80, GPU −9.3 % Sponza, −5 to −6 % Bistros and City Lab) |
| [0058](0058-cluster-front-to-back.md) | Large surfaces' clusters drawn front to back in the camera passes | `geometry/cluster_front_to_back` | REJECT (Sponza's pre-pass −2 %, the frame unchanged: not overdraw-bound) |
| [0059](0059-pipeline-fallback.md) | While a draw's specialization compiles, a compiled pipeline differing only in bits whose other value gives the same image instead of the ubershader | `frame/pipeline_fallback` | REJECT (removes the ubershader where bits change; where materials appear for the first time it adds compilations) |
| [0060](0060-async-ssil-main.md) | With async compute, SSIL recorded on the main queue beside the async SSAO (and SSR where autotune measures it faster), the normal buffer sampled by all three | `frame/async_ss_split` | KEEP, default 3 (Bistro interior −2.7 to −3.8 % GPU, Bistro exterior −0.7 to −1.1 %, Sponza −0.6 to +0.3 %; exact) |
| [0061](0061-shader-creation-at-load.md) | Shaders read from the shader cache created in parallel and only when first asked for | `frame/parallel_shader_create`, `frame/lazy_shader_variants` | KEEP, both on (load to the first frame −12 to −62 % against stock; it was 0.4-1.2 s slower) |
| [0062](0062-graph-visibility-barriers.md) | Render graph: a memory barrier for a read with an unchanged usage from a stage the last write was not made visible to | `debug/visibility_barriers` | KEEP, on (a correctness fix: synchronization validation hazard in light tile culling; no measurable cost) |
| [0063](0063-cluster-lod.md) | Large static surfaces: each cluster drawn at the mesh LOD stock would give it as an instance of its own, crack-free with its neighbours' finer levels | `geometry/cluster_lod` | KEEP as an option (off; changes the image as LOD does: Sponza GPU −3.1 %, render thread +0.21 ms, 63.6 dB on a sequence; no effect in the other scenes) |
| [0064](0064-autotune-undecided-and-creation-frames.md) | Autotune: an undecided evaluation of a tunable that starts off continued next when its pairs favour it; frames that create pipelines or shaders not counted | `autotune/continue_undecided`, `autotune/skip_creation_frames` | KEEP, both on (Lights Lab: tile culling's lowest share of a run 39 % → 79 %; frame times within noise) |
| [0065](0065-hot-workers-linger.md) | Hot workers leave 30 µs after their last chunk instead of 300 µs (spinning through a CPU-bound frame, they made the presentation wait a scheduler quantum: City Lab's 16-45 ms frames); a yield and an idle priority tried | `cpu/hot_workers_usec` (30), `cpu/hot_workers_yield`, `cpu/hot_workers_idle_priority` (off) | KEEP, 30 µs (City Lab 1 % low 56-254 → 325-358 fps, mean frame unchanged between processes; Stress Lab −5 %) |
| [0066](0066-pipeline-prefetch-tunables.md) | Pipelines with the bits autotune switches the other way compiled ahead (low priority) | `frame/pipeline_prefetch_tunables` | REJECT, removed (low-priority tasks left in the WorkerThreadPool hung the engine's exit on lavapipe; at normal priority they compete with the frame's compilations) |
| [0067](0067-ss-effects-undefined-values.md) | SSAO/SSIL: the interleave bounded by the output's size (stock leaves the last column unwritten at widths such as 1920: 1 / (1 / 1920) = 1919.9999), the depth mipmaps' third and fourth levels with one store per texel (stock: four racing stores, two reading past their column) | `postfx/ss_interleave_exact_bounds`, `postfx/ss_depth_mips_exact` | KEEP, both on (RTX: two runs bit for bit in Bistro interior, PBR Lab and Sponza, where stock's differ at 69-82 dB; lavapipe: no read of unwritten memory left in SSAO) |
| [0068](0068-autotune-prepared-pipelines.md) | Autotune: an evaluation of a tunable that is a pipeline bit (the shadow pyramids, light tile culling, persistent instance data) starts once the pipelines of the materials in view with that bit the other way are compiled | `autotune/prepare_pipelines` | KEEP, on (RTX: ubershader draws during the measurement Stress Lab 118-289 → 1, Lights Lab 15-19 → 0, PBR Lab 57-174 → 17-49; exact) |
| [0069](0069-depth-pipeline-keys.md) | Depth passes' pipeline keys without the colour passes' FellowEngine bits, which the depth variants do not compile (autotune's evaluations and the frame's absent features no longer make duplicate depth pipelines) | `frame/depth_pipeline_keys` | KEEP, on (exact; RTX PBR Lab ~20 fewer pipelines per run, half the ubershader draws during the measurement) |
| [0070](0070-clear-new-textures.md) | Textures a pass can write, created without data, cleared before their first use (as stock does on Direct3D 12): a read of texels no pass wrote reads zero | `frame/clear_new_textures` | KEEP, on from `fellow160` (lavapipe: no capture changes with poisoned allocations; RTX: no measurable cost, images unchanged) |
| [0071](0071-shadow-atlas-simulated-time.md) | At a fixed frame rate (`--fixed-fps`, Movie Maker) the positional shadow atlas's 500 ms reallocation tolerance counts the simulated time, not the wall clock (Lights Lab's last run-to-run pixels) | `shadows/atlas_simulated_time` | KEEP, on (RTX: three runs bit for bit in Lights Lab too; with 0067 and 0070, FellowEngine's runs are the same frames in every floor scene) |
| [0072](0072-movie-wait-pipelines.md) | Movie Maker mode: a 3D draw waits for its specialized pipeline instead of drawing with the ubershader (2D already does) | `frame/movie_wait_pipelines` | KEEP, on (only while a movie is written; lavapipe: 0 ubershader draws, two recordings bit for bit) |
| [0073](0073-worker-tasks-six.md) | Worker tasks of the render thread's parallel loops: automatic (half the logical processors less two, 1-6; 6 on the benchmark machine) instead of 10, which left no performance core free for the presentation's threads (City Lab's frames of 6-50 ms) | `cpu/worker_tasks` | KEEP, -1 (City Lab 1 % low 117-281 → 246-289 fps, no mean frame moves) |
| [0074](0074-ssr-resolve-shared.md) | SSR's resolve: each half-resolution sample read and prepared once per workgroup (shared memory) instead of by each of the up to four pixels that use it | `postfx/ssr_resolve_shared` | REJECT, left in, off (the reads shared: exact on the RTX, no gain; the prepared samples shared too: −0.5 % GPU, not exact on the RTX) |
| [0075](0075-taa-strided-load.md) | TAA's 8 × 8 workgroups load their 10 × 10 tile with all 64 invocations (at most two entries each) instead of four each by 25 | `postfx/taa_strided_load` | measuring (off) |
