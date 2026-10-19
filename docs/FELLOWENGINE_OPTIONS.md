# FellowEngine options

All FellowEngine behaviour sits under **Project Settings → Rendering → Fellow** (`rendering/fellow/*`). There are two
kinds of options:

* **Exact** options do not change a single pixel compared with Godot 4.7.2 (verified on lavapipe bit for bit and on
  RTX at the run-to-run floor). They are on by default when they were measured faster, and autotune
  (`autotune/enabled`) switches the scene-dependent ones per scene.
* **Visually lossless** options change the image within measured limits (PSNR against the stock image on still
  captures and on moving camera sequences, the largest per-pixel difference, the share of pixels off by more than two
  levels). They are **off by default**; a project turns them on deliberately.

Every option has a decision record in `docs/decisions/` with its measurements.

## Visually lossless options (off by default)

All four together, against the defaults between processes (`m7_f105_final`, `fellow95`): Lights Lab −25.2 %, Bistro
interior −8.4 %, PBR Lab −7.5 %, Bistro exterior −3.7 %, City Lab −3.5 %, Sponza −1.9 %, Stress Lab −1.5 %, Foliage Lab
+0.3 % (the last four within noise). (`m6_f94_final`: Lights Lab −24.9 %, Bistro interior −9.0 %, PBR Lab −7.0 %.)

| setting | what it does | measured gain (frame, RTX 5070 Ti Laptop, 1080p) | image cost against stock | record |
|---|---|---|---|---|
| `shading/adaptive_vrs` (start-up) | Variable rate shading per 16 × 16 tile, chosen from the previous frame's image and reprojected with the current depth; glossy surfaces, discarding and depth-writing materials keep full rate; autotune keeps it only where it saves time | Lights Lab −21 %, PBR Lab −5 %, Bistro interior −3 % (City Lab, Sponza, Bistro exterior: its passes cost about what it saves, autotune leaves it off) | stills 52.8-74.1 dB (≤ 1.5 % of pixels > 2 levels); sequences 56.9-75.1 dB; a fast pan in Sponza, where autotune leaves it off, 49.8 dB (5.3 % > 2) | 0027 |
| `postfx/ssao_temporal` | SSAO takes half its taps per frame, the other half from the previous frame (reprojected, depth-checked) | −1.5 to −3.1 % where SSAO is the longer async queue | stills 59.3-82.3 dB (min 57.5); sequences 58.3-75.3 dB (min 50.6, 0.37 % > 2) | 0028 |
| `postfx/ssil_temporal` | the same for SSIL | −1.0 to −1.5 % where SSIL is used | stills 58.6-62.9 dB (min 57.8); sequences 57.7-58.7 dB (min 52.1) | 0030 |
| `postfx/fog_half_rate` | volumetric fog lights half its froxels per frame (checkerboard of 4 × 4 × 4 blocks), the rest is reprojected | Lights Lab −1.7 %, elsewhere −0.1 % | stills 61.4-71.3 dB; sequences 60.1-72.2 dB (min 56.5, max \|d\| 10) | 0031 |

For scale: stock's own half-size SSAO against full size is 46-47 dB.

`shading/adaptive_vrs_threshold` (0.006, luma 0-1) is the per-tile error budget; 0 keeps the attachment with every
tile at full rate. `shading/adaptive_vrs_max_step` (0.02) limits the largest step between neighbouring pixels a coarse
tile may have; **0.01 is the strict setting**: every fast pan stays above 62 dB, at the cost of Bistro interior's gain.
`shading/adaptive_vrs_motion_px` (0, off) keeps tiles moving faster than that many pixels per frame at finer rates.
`shading/adaptive_vrs_autotune` (on) lets autotune (0020) decide per scene: adaptive shading starts off and is turned
on only where an evaluation measures it faster. `shading/adaptive_vrs_max_rate` limits the coarsest rate (2 × 2 or 4 × 4).
`shading/adaptive_vrs_refresh` shades every tile at full rate one frame in N so that detail lost to coarse shading is
seen again. `shading/adaptive_vrs_gloss_guard` keeps glossy, curved surfaces at full rate. Adaptive shading needs a
GPU with fragment shading rate attachments (Vulkan `VK_KHR_fragment_shading_rate`), a single view, the depth pre-pass
and no MSAA; otherwise every tile is shaded at full rate.

## Options that change the image (off by default)

| setting | what it does | image cost | record |
|---|---|---|---|
| `geometry/multimesh_instance_lod` (with `geometry/multimesh_culling`) | each MultiMesh instance gets the mesh LOD level stock gives a node of its own (distance to its box, the project's `mesh_lod_threshold`), instead of the level of the MultiMesh's whole box | not visually lossless where many far instances coarsen together: Foliage Lab from above 33-40 dB (far grass clumps lose blades); at ground level 48.2-64.6 dB; −2.2 % with the bench's light LOD meshes | 0035 |
| `geometry/multimesh_cull_vertex_motion` (with `geometry/multimesh_culling`) | MultiMeshes whose material moves vertices (wind) are culled too when their node has an extra cull margin: the margin is taken as how far the vertices move | none while the material keeps within the node's margin (Foliage Lab with wind: 0 differing pixels); beyond it, instances near the view's edges can be lost | 0036 |
| `geometry/cluster_lod` (with `geometry/mesh_clusters`) | each cluster of a large static surface is drawn at the mesh LOD stock would give it as an instance of its own (the main camera's distance to the cluster), with its neighbours' finer levels where they meet (no cracks) | the image as LOD changes it: Sponza 63.6 dB on a moving sequence, stills 52.9-56.8 dB at distant silhouettes; Sponza GPU −3.1 %, render thread +0.21 ms | 0063 |

## Exact options (on by default unless noted)

| area | settings | record |
|---|---|---|
| Shadows | `shadows/directional_cache`, `shadows/skip_*`, `shadows/directional_max_mip`, `shadows/positional_max_mip` | 0004, 0005 |
| Shadows | `shadows/batched_taps` (from `fellow104`: soft shadow filter taps fetched four at a time, stock's arithmetic; frame −2 to −5 %) | 0040 |
| Geometry | `geometry/mesh_clusters` (frustum culling inside large static surfaces) | 0007 |
| Geometry | `geometry/multimesh_culling` (from `fellow93`: MultiMesh instances culled one by one per pass on the GPU; an autotune tunable), `geometry/multimesh_cull_min_instances`, `geometry/multimesh_cull_min_indices` | 0034 |
| Lights | `lights/cluster_depth_slices`, `lights/zero_attenuation_skip`, `lights/cluster_pack_parallel`, `lights/fields_early` (from `fellow115`, v2 `fellow117`: Bistro interior −3.1 %, Lights Lab −3.0 %), `lights/tile_cull` (from `fellow127`, under autotune: Lights Lab −5.6 %; from `fellow132` split around the async compute part, `debug/tile_cull_split`: Bistro interior −1.9 %), `lights/cluster_pack_elements` and `lights/cluster_instanced_draws` (from `fellow130`: the cluster build without its 21.5 MB clear per frame, GPU −1 to −2.3 % where lights, decals or probes are in view) | 0008, 0014, 0045, 0049, 0051 |
| Shading | `shading/blended_alpha_zero_skip`, `shading/absent_features_specialized` (from `fellow142`, 63: the colour passes compiled without the decal, probe, directional, omni, spot and positional shadow code the frame cannot run; GPU −9 % Sponza, −5 to −6 % Bistros and City Lab) | 0010, 0057 |
| Post | `postfx/fast_luminance_reduce`, `postfx/taa_history_pingpong`, `postfx/taa_direct_output`, `postfx/taa_shared_velocity` (start-up), `postfx/ss_last_frame_direct`, `postfx/glow_blur` (from `fellow130`, 2: glow's blur without bank conflicts in 16 × 16 tiles, glow −60 %, GPU −1.1 to −4.5 %), `postfx/tonemap_specialized` (from `fellow138`: tone mapping compiled for the frame's paths only, the pass −12 to −14 %), `postfx/fog_specialized` (from `fellow138`: volumetric fog's density pass without the loops the frame does not run, the pass −21 to −27 %), `postfx/ss_interleave_exact_bounds` and `postfx/ss_depth_mips_exact` (from `fellow158`, correctness: SSAO's last column written at widths such as 1920, where stock leaves it as memory held it; the depth mipmaps without stock's racing stores) | 0025, 0002, 0033, 0044, 0052, 0055, 0056, 0067 |
| Render thread | `cpu/parallel_*`, `cpu/batched_shadow_lists`, `cpu/compact_shadow_instances`, `cpu/persistent_instances`, `cpu/hot_workers_usec` (30 from `fellow156`, was 300: the hot workers no longer spin through a CPU-bound frame; City Lab's 16-45 ms presentation stalls gone, 1 % low 325-358 fps) | 0016, 0018, 0022, 0065 |
| Frame | `frame/parallel_replay`, `frame/present_thread`, `frame/submit_thread`, `frame/async_compute` (NVIDIA), `frame/pipeline_cache_max_mb` (1024; the pipeline cache no longer crashes at 2 GiB), `debug/segment_barriers` (from `fellow134`: barriers between the command buffers of a frame for resources used again with the same usage), `frame/async_ss_split` (from `fellow146`, 3 from `fellow147`: with async compute SSIL runs on the main queue beside the async SSAO, SSR too where autotune measures it faster; Bistro interior −2.7 to −3.8 % GPU), `frame/parallel_shader_create` and `frame/lazy_shader_variants` (from `fellow150`: shaders read from the shader cache created in parallel and only when first asked for; a scene loads to its first frame 12-62 % faster than with stock) | 0015, 0017, 0050, 0054, 0060, 0061 |
| Autotune | `autotune/enabled` (chooses the scene-dependent exact options by measurement), `autotune/continue_undecided` and `autotune/skip_creation_frames` (from `fellow153`: an undecided evaluation of an option that starts off is continued next when it leans on; frames that compile pipelines are not counted) | 0020, 0064 |

`postfx/taa_resolve_v2` (on) is a look change from milestone 1 (TAA with reverse-Z dilation and YCoCg variance
clipping, decision 0001).

## Measured and rejected (left in, off)

Volumetric fog's light loops walked by the subgroup (`postfx/fog_light_loop`, 0053: no frame gain), SSR's roughness filter with a padded shared layout (`postfx/ssr_filter_padded`, 0052: exact, no gain), SSAO/SSIL gather taps fetched in batches (0041), a PCSS blocker search shared by pixel quads (`shadows/pcss_quad_search`, 0042: not visually lossless in Foliage Lab), occlusion culling of the opaque pass (0026), the depth pre-pass in two orders (0029), SSAO/SSIL coherent gathers
(0024), TAA in 16 × 16 groups (0023), instance buffers in system memory (0021), early submit (0009), TAA camera motion
vectors (0013), shared-memory SSAO tiles (0011). Not merged: soft shadows with half their taps under TAA (0032).
