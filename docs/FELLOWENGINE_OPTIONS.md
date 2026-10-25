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

From milestone 16 FellowEngine also adds **visual features** (see *Visual features* below): effects stock Godot does not
have, off by default; with them off the frame is the stock one.

**Determinism.** With the defaults and autotune off, two runs of a scene at a fixed frame rate (`--fixed-fps`, Movie
Maker mode) render the same frames bit for bit on the benchmark's RTX (0067, 0070, 0071; milestone 14), where stock's
runs differ. With autotune on as well, except where a pipeline compiles while the frames are drawn (autotune prepares
its evaluations by compiling pipelines): a draw on the ubershader then depends on the compile threads' timing, as in
stock when a material is first drawn (`m14_f206`: 3 of 4 scenes bit for bit in every pair, Sponza in one of three).

**Godot's own setting worth measuring: the separate render thread** (`rendering/driver/threads/thread_model` =
Separate, or `--render-thread separate`; experimental in Godot). With FellowEngine it shortens the frame where the main
thread does real work: Stress Lab −6.7 %, City Lab −4.5 %, Bistro exterior −1.6 % (`m14_f204`). FellowEngine does not
change the setting; from `fellow170` (0077) a Skeleton3D hands its skin to the render thread in one command instead of
one per bone, which made Crowd Lab (256 animated characters) 25 % faster with the separate thread and the separate
thread faster than the default there (3.47 against 4.34 ms, `m15_f227`).

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

## Visual features (off by default)

Effects that add to the image what a project asks for. Off, they change nothing (verified bit for bit on lavapipe).

| setting | what it does | record |
|---|---|---|
| `post/motion_blur/enabled` (`amount` 0.5, `samples` 12, `max_radius` 32 px) | motion blur after TAA: a reconstruction filter (McGuire 2012, Jimenez 2014) over the velocity buffer, the sky blurred by the camera's motion; works with TAA, MSAA or no anti-aliasing (the colour pass then writes motion vectors for it), not with FSR 2 / MetalFX or in XR | 0078 |
| `post/lens/chromatic_aberration`, `post/lens/vignette` (`vignette_smoothness`), `post/lens/film_grain` | lens effects folded into the tone mapping pass (no pass added): colour fringes towards the edges, darker corners, per-frame grain in the mid-tones; 0 = off | 0079 |
| `post/exposure/histogram` (`low_percent` 10, `high_percent` 90, `speed_up` 3, `speed_down` 1) | with auto exposure on (camera attributes): Unreal's histogram eye adaptation — the mean luminance between two percentiles (the darkest and brightest pixels ignored), adapted exponentially in stops, faster as the exposure rises | 0105 |
| `post/local_exposure/enabled` (`highlight_contrast` 0.8, `shadow_contrast` 0.8, `detail_strength` 1, `blurred_blend` 0.6, `blur_size` 0.5) | Unreal 5's local exposure: large-scale contrast compressed around middle grey (below 1 compresses), local detail kept (a bilateral grid and a wide blur of log luminance) | 0106 |
| `post/contact_shadows/enabled` (`length` 0.3 m, `steps` 8, `thickness` 0.02 m) | screen-space contact shadows of the first directional light (where it casts shadows): what the shadow map misses where objects touch; needs the depth pre-pass, no MSAA; the ray-traced sun shadows replace it | 0107 |
| `post/atmosphere/enabled` (`aerial_perspective` on, `sun_transmittance` on, `aerial_distance_scale` 1, `mie_scale` 1, `rayleigh_scale` 1, `ground_albedo` 0.3, `srgb_rayleigh` on) | Unreal's sky atmosphere (Hillaire 2020; `srgb_rayleigh` off: Unreal's colour coefficients, a bluer sky; `mie_scale` 10-30: a typical day's haze, less blue shade): tables for a sky shader (global shader parameters `fellow_atmosphere_sky_view`, `fellow_atmosphere_transmittance`, `fellow_atmosphere_camera_height`; see `benchmarks/project/bench/fellow_atmosphere_sky.gdshader`), aerial perspective on the opaque surfaces, the sun's colour through the atmosphere | 0108 |
| `post/clouds/enabled` (`coverage` 0.45, `density` 1, `altitude` 1.5 km, `thickness` 2.5 km, `wind` 0.01 km/s, `interleaved` on) | volumetric clouds over the sky's pixels (half resolution, temporal; interleaved: a quarter of the pixels traced per frame); lit through the atmosphere when it is on | 0109, 0115 |
| `post/sharpen/strength` (0-1) | contrast-adaptive sharpening (RCAS) of the TAA output, in the tone mapping pass; only with TAA | 0110 |
| `post/bloom/lens_dirt` | the environment's glow map as a lens dirt mask, added where the glow is (Unreal's bloom dirt) — on FellowEngine's bloom when it is on, else on Godot's glow; needs a glow map | 0111 |
| `post/bloom/enabled` (`intensity` 0.05, `scatter` 0.7) | an energy-conserving bloom of the whole image (no threshold or cap: the sun's disk glows in proportion), mixed into the image by `intensity`; independent of Godot's glow | 0112 |
| `post/bloom/lens_flare` (`lens_flare_threshold` 8) | lens flare ghosts and halo of the sources above the threshold (scene-linear); needs the bloom | 0113 |
| `post/white_balance/temperature` (6500 K), `tint` (0) | white balance as a camera's or Unreal's: the scene's white shown white (8000 K warms a sky-lit scene, 5000 K cools it; a positive tint turns it magenta) | 0114 |
| `post/light_shafts/intensity` (`threshold` 0.5, `length` 0.9) | rays from the sun: the bright sky around it blurred towards it, broken by roofs, trees and clouds | 0116 |
| `raytracing/enabled` (start-up; `radius` 200 m, `skinned`, `multimesh` on, `multimesh_min_angle` 1°) | hardware ray tracing with ray queries (a GPU with `VK_KHR_ray_query`): acceleration structures for the meshes and MultiMesh instances near the camera (MultiMesh instances seen under less than the angle left out), rebuilt per frame; alone it changes nothing, the effects below use it | 0080 |
| `raytracing/ambient_occlusion` (`ao_rays` 2, `ao_interleaved` on) | ray-traced ambient occlusion in place of SSAO, where the environment enables SSAO (its radius, intensity, power); interleaved: a quarter of the pixels traced per frame | 0081 |
| `raytracing/sun_shadows` (`sun_shadow_rays` 1, `sun_shadows_interleaved` on) | the first directional light's shadow traced per pixel within half the radius (soft over the light's angular size), in place of the shadow map there; interleaved: a quarter of the pixels traced per frame | 0082 |
| `raytracing/gi` (`gi_probe_spacing` 2 m, `gi_probes_xz` 24, `gi_probes_y` 8, `gi_rays` 128, `gi_hysteresis` 0.97, `gi_energy` 1, `gi_update_interval` 4, `gi_sky_leak` 0.1, `gi_probe_relocation` on, `gi_cascades` 2; debug `raytracing_gi_reference`) | dynamic diffuse global illumination: a volume of probes around the camera traced continuously (a quarter of the probes per frame; sun, sky, emission, multiple bounces, the materials' colour × their albedo texture's mean; probes moved out of geometry; a coarser volume, 4 × the spacing, where the fine one ends), in place of the ambient light in every colour pass (the opaque passes read it from a screen pass); the debug setting shows a path-traced reference instead | 0083 |
| `raytracing/reflections` (`reflections_max_roughness` 0.7, `reflections_interleaved` on) | where the environment enables SSR, reflections traced against the ray tracing scene instead (surfaces up to the maximum roughness; off-screen and occluded objects reflected, lit by the sun and DDGI) | 0084 |

## Gas volumes: smoke and fire (milestone 20)

A `FellowGasVolume3D` node (module `fellow_fluid`) simulates smoke and fire in a box on the GPU — one step per physics
tick, the same steps on every run — and the Forward+ renderer draws it; its `FellowGasEmitter3D` children (sphere, box,
cylinder) emit smoke, fuel and heat, the physics bodies inside it are obstacles the gas flows around, and an omni light
the node places at the fire lights the scene. Without such a node nothing of it runs. Decision records 0117
(simulation) and 0118 (rendering); bench scene Fire Lab (`--scene=fire`).

| property (node) | what it does |
|---|---|
| `size`, `resolution` (64), `high_precision`, `space` (local) | the box (its floor at the node), the cells along its longest side; RGBA32F state instead of RGBA16F; world: the gas stays in the world, the box follows the node by whole cells (a carried torch) |
| `closed_faces` (the floor) | walls; the other faces are open (fresh air), or an inflow where the `wind` blows in |
| `buoyancy` (1: an ideal gas's), `smoke_weight`, `vorticity` (0.3), `turbulence`, `turbulence_scale`, `wind`, `wind_rate` | the forces |
| `smoke_dissipation`, `cooling`, `ignition_temperature` (250 K), `burn_rate`, `heat_release` (1400 K), `soot`, `expansion` | the smoke's life and the combustion: fuel burns above the ignition temperature into heat, soot and expansion |
| `pressure_solver` (multigrid; SOR), `multigrid_cycles` (2), `pressure_iterations` (20, SOR's), `maccormack` (on), `substeps`, `time_scale`, `prewarm_steps` | the solver |
| `physics_obstacles`, `collision_mask` | the bodies inside the box as obstacles (spheres, boxes, capsules, cylinders, world boundaries; convex shapes as their box; at most 16); a body moved faster than six cells per step counts as teleported |
| `smoke_color`, `smoke_density` (4 / m), `anisotropy`, `ambient_strength`, `self_shadow`, `render_step` | the smoke's look |
| `fire_intensity` (20), `fire_temperature_scale` (1.4) | the fire's blackbody emission (brightness by the fourth power of the temperature) and its colour's temperature |
| `cast_shadows`, `shadow_strength` | the sun's shadow through the smoke on the scene (in the contact shadow mask; not with the ray-traced sun shadows) |
| `fire_light`, `fire_light_energy`, `fire_light_range`, `fire_light_shadows` | the omni light at the fire, from the simulated emission (2-3 frames late) |
| `velocity_texture_enabled`, `velocity_texture_scale` (5 m/s) | the velocity at the cell centres as a `Texture3D` (`get_velocity_texture()`) for a `GPUParticlesAttractorVectorField3D` over the box: sparks and embers carried by the flow (Fire Lab's embers) |

Project settings: `fluid/enabled` (the simulation), `fluid/gas_rendering` (the drawing), `fluid/gas_solver`
(measurements: 0 the node's solver, 1 SOR, 2 multigrid). Limits: transparent objects
behind the smoke are drawn over it; the smoke is not in the volumetric fog; the scene's shadows do not darken the smoke;
one view (no XR), no MSAA.

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
| Shadows | `shadows/atlas_simulated_time` (from `fellow160`: at a fixed frame rate — `--fixed-fps`, Movie Maker — the positional shadow atlas waits 500 ms of the scene's time, not of the wall clock, before giving a light a shadow of another size: the same frames on every run) | 0071 |
| Geometry | `geometry/mesh_clusters` (frustum culling inside large static surfaces) | 0007 |
| Geometry | `geometry/multimesh_culling` (from `fellow93`: MultiMesh instances culled one by one per pass on the GPU; an autotune tunable), `geometry/multimesh_cull_min_instances`, `geometry/multimesh_cull_min_indices` | 0034 |
| Lights | `lights/cluster_depth_slices`, `lights/zero_attenuation_skip`, `lights/cluster_pack_parallel`, `lights/fields_early` (from `fellow115`, v2 `fellow117`: Bistro interior −3.1 %, Lights Lab −3.0 %), `lights/tile_cull` (from `fellow127`, under autotune: Lights Lab −5.6 %; from `fellow132` split around the async compute part, `debug/tile_cull_split`: Bistro interior −1.9 %), `lights/cluster_pack_elements` and `lights/cluster_instanced_draws` (from `fellow130`: the cluster build without its 21.5 MB clear per frame, GPU −1 to −2.3 % where lights, decals or probes are in view) | 0008, 0014, 0045, 0049, 0051 |
| Shading | `shading/blended_alpha_zero_skip`, `shading/absent_features_specialized` (from `fellow142`, 63: the colour passes compiled without the decal, probe, directional, omni, spot and positional shadow code the frame cannot run; GPU −9 % Sponza, −5 to −6 % Bistros and City Lab) | 0010, 0057 |
| Post | `postfx/fast_luminance_reduce`, `postfx/taa_history_pingpong`, `postfx/taa_direct_output`, `postfx/taa_shared_velocity` (start-up), `postfx/ss_last_frame_direct`, `postfx/glow_blur` (from `fellow130`, 2: glow's blur without bank conflicts in 16 × 16 tiles, glow −60 %, GPU −1.1 to −4.5 %), `postfx/tonemap_specialized` (from `fellow138`: tone mapping compiled for the frame's paths only, the pass −12 to −14 %), `postfx/fog_specialized` (from `fellow138`: volumetric fog's density pass without the loops the frame does not run, the pass −21 to −27 %), `postfx/ss_interleave_exact_bounds` and `postfx/ss_depth_mips_exact` (from `fellow158`, correctness: SSAO's last column written at widths such as 1920, where stock leaves it as memory held it; the depth mipmaps without stock's racing stores) | 0025, 0002, 0033, 0044, 0052, 0055, 0056, 0067 |
| Render thread | `cpu/parallel_*`, `cpu/batched_shadow_lists`, `cpu/compact_shadow_instances`, `cpu/persistent_instances`, `cpu/hot_workers_usec` (30 from `fellow156`, was 300: the hot workers no longer spin through a CPU-bound frame; City Lab's 16-45 ms presentation stalls gone, 1 % low 325-358 fps), `cpu/worker_tasks` (automatic from `fellow163`, 6 on the benchmark machine, was 10: City Lab's remaining presentation stalls gone, 1 % low 246-289 fps) | 0016, 0018, 0022, 0065, 0073 |
| Frame | `frame/parallel_replay`, `frame/present_thread`, `frame/submit_thread`, `frame/async_compute` (NVIDIA), `frame/pipeline_cache_max_mb` (1024; the pipeline cache no longer crashes at 2 GiB), `debug/segment_barriers` (from `fellow134`: barriers between the command buffers of a frame for resources used again with the same usage), `frame/async_ss_split` (from `fellow146`, 3 from `fellow147`: with async compute SSIL runs on the main queue beside the async SSAO, SSR too where autotune measures it faster; Bistro interior −2.7 to −3.8 % GPU), `frame/parallel_shader_create` and `frame/lazy_shader_variants` (from `fellow150`: shaders read from the shader cache created in parallel and only when first asked for; a scene loads to its first frame 12-62 % faster than with stock), `frame/depth_pipeline_keys` (from `fellow158`: the depth passes' pipeline keys without bits their code does not read: fewer duplicate pipelines), `frame/clear_new_textures` (from `fellow160`, correctness: a texture a pass can write, created without data, is cleared before its first use, as stock does on Direct3D 12 — no image depends on what memory held), `frame/movie_wait_pipelines` (from `fellow161`: while Movie Maker mode writes a video, a 3D draw waits for its pipeline instead of drawing with the ubershader — recordings are the same on every run) | 0015, 0017, 0050, 0054, 0060, 0061, 0069, 0070, 0072 |
| Animation | `animation/elide_modifier_backup`, `animation/full_weight_slerp`, `animation/track_shortcuts`, `animation/key_hints` (from `fellow168`, key hints from `fellow170`: skeletal animation on the main thread without work whose result is known — no bone backup around modifiers that change nothing, such as every Skeleton3D's compatibility physical-bone simulator; slerp at weight 1 without its trigonometry; per-track lookups done once; the same bone poses bit for bit; Crowd Lab, 256 animated characters, frame −16 to −20 %), `animation/batched_skin_transforms` (from `fellow170`: a skin's bone transforms handed to the RenderingServer in one call; with the separate render thread Crowd Lab −25 %) | 0076, 0077 |
| Physics | `physics/distance_face_cull`, `physics/distance_face_order` (from `fellow183`: GodotPhysics3D's shape casts and `move_and_slide()` motion tests skip the trimesh faces a conservative test proves out of reach; ShapeCast3D-heavy lab ×4), `physics/convex_vertex_cache`, `physics/parallel_pair_culls` (broadphase culls on worker threads), `physics/parallel_island_solve` (a large island solved on every thread in stock's per-body order), `physics/island_constraint_lists`, `physics/jolt_deferred_sensor_contacts`, `physics/jolt_max_jobs` (-1: Jolt's jobs by the work), `physics/motion_query_cache` (a repeated motion test within one `move_and_slide()` answered from the first), `physics/distance_face_shape_axes`, `physics/distance_face_cull_cache`, `physics/distance_face_sweep_reuse` (from `fellow189`: the face test bounds a capsule or sphere by itself, with the faces' edge axes; faces culled once per query; faces out of reach of a sweep not tested again for the sweeps within it), `physics/ray_prune` (raycasts skip trimesh nodes beyond their closest hit); always on: vectorised and inline segment-box tests, `Vector3::is_finite()` inline. The physics state is the same as stock's tick by tick (Physics Lab hashes, any worker count); `physics/verify` checks the skipped faces | 0085-0091, 0093, 0094, 0096-0100, 0103 |
| Autotune | `autotune/enabled` (chooses the scene-dependent exact options by measurement), `autotune/continue_undecided` and `autotune/skip_creation_frames` (from `fellow153`: an undecided evaluation of an option that starts off is continued next when it leans on; frames that compile pipelines are not counted), `autotune/prepare_pipelines` (from `fellow158`: an evaluation that switches a pipeline bit waits until the pipelines of the materials in view with the other value are compiled, instead of drawing with the ubershader in its first frames) | 0020, 0064, 0068 |

`postfx/taa_resolve_v2` (on) is a look change from milestone 1 (TAA with reverse-Z dilation and YCoCg variance
clipping, decision 0001).

## Fast physics mode (off by default; not stock's results)

Settings under `rendering/fellow/physics/fast/`, each off by default. Deterministic (the same results run to run and with
any number of threads) and physically equivalent to stock within the tolerance its record states, measured on Physics
Lab with `benchmarks/tools/physics_deviation.py`; not stock's results bit for bit.

| setting | what | record |
|---|---|---|
| `physics/fast/jolt_shape_cast` | Jolt's motion queries (`ShapeCast3D`, `move_and_slide()`) by one shape cast per body instead of a bisection of up to 16 collision tests; shape casts' fractions within 2.5 mm on 25 m | 0095 |
| `physics/fast/godot_toi` | GodotPhysics3D's motion queries by the time of impact (GJK ray casting) instead of an 8-step bisection (whose own resolution is 1/256 of the motion) | 0102 |

## Measured and rejected (left in, off)

Volumetric fog's light loops walked by the subgroup (`postfx/fog_light_loop`, 0053: no frame gain), SSR's roughness filter with a padded shared layout (`postfx/ssr_filter_padded`, 0052: exact, no gain), SSR's resolve with its samples shared per workgroup (`postfx/ssr_resolve_shared`, 0074: exact, no gain; sharing their preparation too, −0.5 % GPU, rounds otherwise on the RTX), SSAO/SSIL gather taps fetched in batches (0041), a PCSS blocker search shared by pixel quads (`shadows/pcss_quad_search`, 0042: not visually lossless in Foliage Lab), occlusion culling of the opaque pass (0026), the depth pre-pass in two orders (0029), SSAO/SSIL coherent gathers
(0024), TAA in 16 × 16 groups (0023), Jolt's jobs through a lock-free queue and pump tasks (`physics/jolt_job_pumps`, 0092: exact, no consistent gain), TAA's tile loaded by all 64 invocations of its 8 × 8 groups (`postfx/taa_strided_load`, 0075: exact, no gain), instance buffers in system memory (0021), early submit (0009), TAA camera motion
vectors (0013), shared-memory SSAO tiles (0011). Not merged: soft shadows with half their taps under TAA (0032).
