# FellowEngine options

All FellowEngine behaviour sits under **Project Settings → Rendering → Fellow** (`rendering/fellow/*`; crash reports,
milestone 97, under **Debug → Fellow**, `debug/fellow/*`). There are two kinds of options:

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
thread faster than the default there (3.47 against 4.34 ms, `m15_f227`). Milestone 60 (decision 0165): the water, rain
and shallow water nodes free their GPU resources on the render thread (they leaked with a separate one), and a deadlock
in Godot's shader compilation that a two-thread worker pool reached with it is fixed. Measure with
`--rs_metrics=render_thread` (the bench's reads of render times otherwise wait for the render thread every frame).

**Crowds on worker threads** (decision 0164): characters under a node whose `process_thread_group` is Sub-Thread are
posed, and their skeletons updated, on a worker thread in parallel with the other groups; FellowEngine's animation
settings give the same poses there bit for bit. A scene's choice (Crowd Lab `--crowd_groups`, Slice Lab
`--slice_crowd_groups`).

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
| `post/clouds/enabled` (`coverage` 0.45, `density` 1, `altitude` 1.5 km, `thickness` 2.5 km, `wind` 0.01 km/s, `interleaved` on, `shadows` 1, `sky_light` 1) | volumetric clouds over the sky's pixels (half resolution, temporal; interleaved: a quarter of the pixels traced per frame); lit through the atmosphere when it is on; their shadows on the scene's surfaces (a map of the sun through the layer, 16 km around the camera; `shadows` scales them, 0: none); the sky's ambient light and reflections greyed where they cover it (`sky_light`) | 0109, 0115, 0133 |
| `post/clouds/detail` (on) | the clouds' edges eroded into billows (the detail noise's octaves stretched to [0, 1] — unstretched they varied the erosion by a tenth — at features of 150-600 m, eroded deeper), traced in fine steps inside the clouds (25 m near the camera; coarse ones through clear air, at most 160 a ray, 64 even ones before), the billows shading one another (the first three light samples eroded too) and Schneider's powder term away from the sun; off: decision 0109's clouds bit for bit | 0310 |
| `post/clouds/realism` (on) | with the detail: the clouds' edges sharp (their density ramp eight times steeper at the tops, three at the bottoms), bumped in and out by two finer octaves (cauliflower of 40-150 m, turbulence of 10-40 m, as far as they are a pixel or more) through a warped field, their outlines roughened (the weather's finer octaves, the shapes warped); multiple scattering by diffusion (a thick cloud's sunlit side 0.8 E / π, falling off as 1 / (1 + 3/4 (1 − g) τ)), a billow's side turned from the sun dimmed, the sky's light diffused through the cloud above; the aerial perspective from the atmosphere's tables instead of a fade; a Catmull-Rom upsampling; finer steps (12 m near the camera); off: decision 0310's clouds bit for bit | 0324 |
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
| `sdfgi/screen_gather` (`screen_gather_tile` 16 px, `screen_gather_rays` 16, `screen_gather_history` 16 frames, `screen_gather_filter` on, `screen_gather_persistent` on, `trace_max_steps` 64, `trace_surface_bias` 1 cell) | where the environment enables SDFGI (no ray tracing needed): its diffuse light gathered per screen probe instead of interpolated from its world probes — a probe every 16 x 16 pixels traces rays from its surface through SDFGI's distance field cascades (the voxels' light at the hits: every light, emission, SDFGI's bounce; the sky beyond), averaged over frames where its surface stays in view and over its neighbours on the same surface; SDFGI's reflections stay. Against the path-traced reference: 0.34 stops off a pixel (SDFGI 0.41; GI Lab 0.10 from 0.41), a quarter of SDFGI's leak through thin walls; +0.3 ms at 1080p | 0308 |
| `sdfgi/screen_gather_persistent` (on; both gathers) | a screen probe keeps its point and its history while that point is still seen in its tile (a pixel within one of its projection on its plane), instead of starting again from its tile's centre pixel each frame, where the TAA's jitter moved an edge across it (folds, leaves, slats) and its history was lost; its rays around the geometric normal. GI Arcade on lavapipe: the flicker the gathers add 0.71 → 0.07 % (ray-traced) and 0.73 → 0.17 % of a still view's calm pixels, as DDGI's (0.04 %) and SDFGI's (0.15 %); the light unchanged | 0309 |
| `sdfgi/screen_gather_pixel_history` (8 frames; 1: none; both gathers) | the gather's result averaged per pixel over the frames its surface point stayed in view: the previous frame's result where the point was then (the camera's motion), kept where it lay at the point's distance from the previous eye (another surface otherwise: a disocclusion), blended by 1/n. Under a moving camera the probes a pixel reads change (new tiles, new points, a probe's first frames) and a flat shaded wall shimmered through the TAA's clamp: lavapipe's shaded walls at a run's speed 11.2 → 10.5 % of the pixels by more than 3 levels, 4.1 → 3.9 % by more than 8; the light unchanged; 24 bytes a pixel of the ambient buffer (50 MB at 1080p) | 0333 |
| `raytracing/gi_screen_gather` (the tile, rays, history and filter of `sdfgi/screen_gather_*`) | with `raytracing/gi` and its screen pass: the first bounce traced per screen probe against the ray tracing scene (its hits lit by the sun with a shadow ray and by DDGI's probes for the further bounces) in place of the probes' interpolated light, into DDGI's screen pass texture; `gi_sky_leak` is then not added. Against the path-traced reference: 0.29 stops off a pixel (DDGI 0.94-1.10); +0.7 ms at 1080p; flickered on Sponza's curtains (10.9 % of a still view's pixels, DDGI 3.8 %) until `sdfgi/screen_gather_persistent` (0309) | 0308 |
| `raytracing/gi_screen_gather_feedback` (on) | with `raytracing/gi_screen_gather`: a gather ray's hit takes its further bounces from the previous frame's screen probes where they saw it (2 x 2 around its projection, on its surface), DDGI's probes elsewhere. GI Lab's room on lavapipe +0.35 → +0.25 stops against the path tracer (DDGI +0.86) | 0309 |
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
| `sparse` (on), `sparse_margin` (2 bricks) | only the 4³ bricks within the margin of smoke, heat, fuel or an emitter simulated (the velocity one brick further), the air beyond still (the wind, zero pressure) — 0119: Fire Lab's simulation −51 % at 128 cells, −11 % at 64 |
| `physics_obstacles`, `collision_mask` | the bodies inside the box as obstacles (spheres, boxes, capsules, cylinders, world boundaries; convex shapes as their box; at most 16); a body moved faster than six cells per step counts as teleported |
| `smoke_color`, `smoke_density` (4 / m), `anisotropy`, `ambient_strength`, `self_shadow`, `render_step` | the smoke's look |
| `edge_fade` (0 m) | the smoke drawn faded out within this distance of the box's faces (all but a closed floor), so the box does not show where the smoke leaves it or piles against a wall; only the drawing (and the smoke's shadows) |
| `fire_intensity` (20), `fire_temperature_scale` (1.4) | the fire's blackbody emission (brightness by the fourth power of the temperature) and its colour's temperature |
| `cast_shadows`, `shadow_strength` | the sun's shadow through the smoke on the scene (in the contact shadow mask; not with the ray-traced sun shadows) |
| `fire_light`, `fire_light_energy`, `fire_light_range`, `fire_light_shadows`, `fire_light_smoothing` (0 s) | the omni light at the fire, from the simulated emission (2-3 frames late); smoothing eases it (and its range four times slower) so the ground does not flicker with every step's sum of the turbulent flames |
| `velocity_texture_enabled`, `velocity_texture_scale` (5 m/s) | the velocity at the cell centres as a `Texture3D` (`get_velocity_texture()`) for a `GPUParticlesAttractorVectorField3D` over the box: sparks and embers carried by the flow (Fire Lab's embers) |
| `flame_temperature_limit` (0: none; 2200 K above ambient where burning objects feed the volume) | the combustion makes no cell hotter (the fuel left burns further on): burning objects' fuel cannot run the gas away (0120) |

Project settings: `fluid/enabled` (the simulation), `fluid/gas_rendering` (the drawing), `fluid/gas_scene_shadows`
(the sun's shadow map on the smoke, on); for measurements: `fluid/gas_solver` (0 the node's solver, 1 SOR, 2
multigrid), `fluid/gas_sparse` (0 the node's, 1 off, 2 on), `fluid/gas_sparse_margin` (0 the node's),
`fluid/gas_mg_tail_cells` (0: the multigrid's levels of at most that many cells in one workgroup; measured slower,
0119), `fluid/gas_profile_passes` (a timestamp before each group of a step's passes).
Limits: transparent objects behind the smoke are drawn over it; the smoke is not in the volumetric fog; one view (no
XR), no MSAA.

## Burning objects (milestone 22)

A `FellowBurnable3D` node (module `fellow_fluid`; a child of a `MeshInstance3D`, or `mesh_path`) makes the mesh burn
like wood in a gas volume's fire: the mesh is voxelized, the GPU heats the voxels from the flames, pyrolyses the wood
to char (the volatiles feed the gas's flames: the fire comes from the object), oxidises the exposed char to glowing
embers and ash, and lets the ash fall into a heap on the ground below; the mesh recedes with the material, darkens,
glows in its cracks and is burned through where it is thin, the ash drawn as a heap of its own (`FellowAshHeap`).
Runs only in a running game; it replaces the mesh instance's mesh and materials while in the tree. Its gas volume:
`gas_volume_path`, or the one holding the object's middle (milestone 211: the mesh's, not the voxel box's, which
reaches down to the ground for the ash heap). Decision record 0120; bench scene Burn Lab (`--scene=burn`).

| property (node) | what it does |
|---|---|
| `mesh_path` (the parent), `gas_volume_path` (the volume holding the object's centre), `resolution` (32 voxels along the longest side), `subdivide` (on) | the object, its fire, its voxels; the drawn mesh subdivided to 1.5 voxels per edge so it can follow them |
| `burn_speed` (10) | burning seconds per second: a log burns in about a minute, not an hour (the flames keep their real size) |
| `density` (500 kg/m³), `specific_heat`, `conductivity`, `pyrolysis_temperature` (573 K), `pyrolysis_rate`, `char_yield`, `glow_temperature` (673 K), `glow_rate`, `ash_volume` (0.15) | the wood: how fast it heats through (thin parts first), chars, glows and how much ash it leaves |
| `flame_temperature` (1100 K), `flame_reach` (10 cm) | the surface's own flame (thinner than a voxel): a burning surface exchanges heat with it, the flame climbs over the wood above and creeps sideways — what keeps the object burning to ash on its own; lower to make it go out by itself |
| `flame_strength` (1: 2.7 of the gas's fuel units per kg of volatiles), `flame_attachment` (0.5), `flame_flicker` (0.7), `surface_heating` (200/s), `smoke` (0.05) | the coupling with the gas: the visible flames' size, the part burning at the surface, how much they lick (the fuel released flickering), the air heated by the hot surface, the smoke |
| `collapse_fraction` (0.2) | with less wood and char left the object's physics body stops colliding (its shapes disabled, a rigid body frozen); signal `collapsed` |
| `char_color`, `ash_color`, `glow_intensity` (4), `crack_scale` (1.2 cm) | the look: the char's alligator cracks (along the grain, the mesh's longest axis), the ash coating it, the embers breathing in the cracks; the heap's charcoal pieces in grey ash |

Methods `ignite(position, radius, temperature)`, `reset_burning()`, `get_wood_fraction()` / `get_char_fraction()` /
`get_ash_fraction()`, `is_burning()`, `is_collapsed()`; signals `ignited`, `burned_out`, `collapsed`. Limits: at most 8
objects per gas volume; skinned meshes are simulated, not drawn burning.

Project setting `fluid/burn_vertices_cached` (on): the drawn mesh's vertices march into the voxels once per frame
with steps, in a compute pass, instead of in every pass that draws them (the depth pre-pass, the opaque pass, each
shadow cascade); a material whose vertex code changes `VERTEX` or `NORMAL`, or works in world space, marches as
before. Off: the march in the materials.

## Far objects: automatic HLOD (milestone 23)

A `FellowHLOD3D` node (module `fellow_world`) replaces the static meshes below it beyond `distance` by proxies, built
when the node is ready (or by `build()`):

* **instanced proxies**: a mesh repeated at least `instance_threshold` (32) times with the same material override and
  instance settings becomes one MultiMesh per kind, culled instance by instance on the GPU and drawn only from
  `distance` on (each mesh node up to it). The same mesh, material and transform either way: City Lab gives the same
  image bit for bit (lavapipe) with the render thread 23-42 % shorter. Needs `geometry/multimesh_culling` (on by
  default) and materials the GPU can cull (`BaseMaterial3D` without billboard, grow, point or fixed size);
* **merged proxies** for the rest: per cell (`cell_size`, 50 m) and material, one mesh with generated LODs
  (`generate_lods`), switching per cell with `margin` as hysteresis. They pay for unique meshes; repeated ones are
  better instanced (merging undoes Godot's instancing: City Lab's 18 800 meshes as 2 300 merged surfaces drew 1 500
  draws instead of 200).

* **impostors** (`impostor_distance`, 0 = off): meshes of at least `impostor_min_triangles` (2 000) with an instanced
  proxy, and the `MultiMeshInstance3D` nodes below the node, get octahedral impostors baked at run time
  (`impostor_frames`² views of `impostor_resolution` pixels, albedo and normals) and drawn from `impostor_distance` on
  as camera-facing quads blending the four nearest views. They change the image (far trees as pictures: Foliage Lab
  aerial 27.4 dB, mean colour within 1.4 levels) and pay for **heavy meshes only**: an impostor is an alpha-tested
  quad over the bounding sphere, so it trades vertices for fragments. Foliage Lab's 420-triangle trees beyond 60 m:
  flythrough −2.8 % on the RTX, but the aerial view +5 % and the forest +6 % (lavapipe forest +85 %); baking 8 x 8
  views of a mesh takes about 70 frames at start. Measure with your own meshes before using them. Milestone 129
  (decision 0253): `bake_impostors(directory)` bakes them into files ahead (Project > Tools > Bake HLOD Impostors in
  the editor), kept in the node's `impostors` (by the mesh's and material override's files, `impostor_frames` and
  `impostor_resolution`) and taken by `build()` at once: no frames of baking at start. Signal `impostors_baked`.
  Milestone 130 (decision 0254): `bake_merged_proxies(directory)` bakes the merged proxies too (`merged_proxies`, by a
  signature of each cell's members: their meshes' and materials' files and transforms); `build()` takes a cell's mesh
  when its members are as baked (a town of 1 600 houses: 2.5 s → 9 ms). Project > Tools > Bake HLOD bakes both.
  Milestone 132 (decision 0256): `fade` (off): the merged proxies' members fade over `margin` on each side of
  `distance` while their proxy shows behind them (`VISIBILITY_RANGE_FADE_DEPENDENCIES`), instead of switching — with
  `geometry/dithered_fade` and TAA or FSR, dithered and solid. Milestone 133 (decision 0257): the instanced proxies
  too — their members fade out by themselves, the proxy's instances fade in on the GPU, and hand over to their
  impostors the same way. City Lab `--city_hlod_fade=1 --city_hlod_margin=<m>`.

Directional cascades starting beyond `distance` draw the proxies, the nearer ones the meshes, independently of the
camera's position (`shadow_mode`), so the directional shadow cache keeps both. Meshes with skins, blend shapes,
overlays, transparency, their own visibility range or parent, or lights and particles below them are left as they
are; put a mesh in the group `fellow_hlod_exclude` to keep it out; lightmapped meshes lose their lightmap in a proxy.
The meshes must not move while built. `get_build_info()` reports what was merged. Decision record 0122; bench
City Lab `--city_hlod=<distance>`.

## Water bodies (milestone 24)

A `FellowWater3D` node (module `fellow_fluid`) is a body of water at its height, to the horizon or within `extent`:

* **wind waves** from a JONSWAP spectrum (`wind_speed`, `wind_direction`, `fetch`: a small lake's short waves, the
  sea's long ones), simulated on the GPU by FFT in up to four cascades (`cascade_lengths`, 160 / 27 / 5 m;
  `wave_resolution` 256), `choppiness` for sharp crests, foam where they break (`whitecap_threshold`, `foam_decay`,
  `foam_gain`); `time_auto` off to drive the clock yourself (`time`);
* a **FellowShallowWater3D** (`simulation_path`) drawn as part of it where it covers the body: its surface, slopes and
  foam, its flow carrying the waves; blended in over `simulation_blend` from its edges; its own mesh hidden;
* **shading**: screen-space reflections (`screen_space_reflections`, `reflection_steps`, `reflection_distance`;
  traced hierarchically over the depth texture's nearest-depth mip levels, `postfx/depth_copy_nearest_mipmaps`, 0126)
  with the screen's view of distant scenery and the sky as fallback, Fresnel, refraction (`refraction_strength`),
  absorption and in-scattering (`absorption` per metre, `scattering_color`), shore foam (`shore_foam_depth`),
  caustics on the bottom (`caustics_strength`, `caustics_depths`), the sun's glitter path (`roughness` plus the waves
  a pixel does not resolve); `debug_view` shows the terms one by one.

Cost (RTX 5070 Ti Laptop, 1080p, Water Lab): 1.6 ms of the frame (Godot's water 0.38 ms), of it the reflections
0.93 ms — 0.61 ms with 0126's hierarchical march (fellow208: Water Lab 2.76 ms of GPU time instead of 3.13;
`screen_space_reflections` off, fewer `reflection_steps`, a higher `reflection_min_fresnel` trade them), the
waves' compute 0.10 ms, the caustics 0.03 ms. The surface is a polar grid around the camera (`grid_segments`,
`grid_near`, `grid_far`). The node's rotation and scale are not used. `rendering/fellow/fluid/enabled` off stops the waves. Decision record 0123; bench Water Lab
(`--scene=water`, `--water=full|basic|none`).

### Floating on the waves (milestone 175)

`FellowWater3D.get_surface_height(position)` and `get_surface_velocity(position)` (decision 0317): the surface drawn
over a point and the water's motion there, from the waves on the CPU (the cascades of 10 m and longer, from the GPU's
own modes on a 16-64 grid; recomputed when asked at a new time, about 0.3 ms); `-INF` outside `extent`.
`get_cpu_wave_stats()`, `debug_compare_cpu_waves(cascade)` (against the GPU's texture).

`FellowBuoyancy3D` (module `fellow_fluid`), a child of a `RigidBody3D`: its collision shapes filled with points, each
pushed up by the water it displaces and dragged towards the water's motion.

| property | default | meaning |
|---|---|---|
| `water_path` | empty | the `FellowWater3D` (empty: the one whose extent holds the body) |
| `water_density` | 1000 kg/m³ | the water's density (sea water 1025): the body floats if its mass over its volume is less |
| `point_spacing`, `max_points` | 0.25 m, 64 | the points filling the shapes (the spacing made coarser until they fit) |
| `linear_drag`, `angular_drag` | 3, 3 1/s | the water's resistance at full submersion (in proportion to the share under water) |
| `follow_waves` | true | the drag against the motion relative to the water's (the waves' orbital velocity) |

Methods: `get_submerged_share()`, `get_volume()`, `get_point_count()`, `get_stats()`, `rebuild_points()`. Measured with
`benchmarks/tools/buoyancy/buoyancy.gd` (`--set=linear_drag:2,...`); `gpu_match.gd` and `demo.gd` need a Vulkan device
(lavapipe).

### Water interaction (milestone 176)

Decision 0318. The CPU's waves are made on a worker thread after each frame's process while something asked for the
surface (the main thread waits only if the next physics tick comes first); `get_surface_ceiling()`: the level plus six
times their rms height — `FellowBuoyancy3D` skips a body above it.

| node | property | default | meaning |
|---|---|---|---|
| `FellowWater3D` | `body_foam_size` | 64 m | the square around the camera where foam added by `add_foam()` (splashes, wakes) is kept and drawn: 256 × 256 cells; 0: none |
| `FellowWater3D` | `body_foam_decay` | 0.3 1/s | how fast that foam fades (half in 2.3 s) |
| `FellowBuoyancy3D` | `foam` | 1.0 | the foam the body makes: splashes where its points strike the water faster than 1 m/s, a wake where they cross the waterline moving faster than 0.4 m/s; 0: none |
| `FellowDestructible3D` | `debris_floats` | true | while the scene has a `FellowWater3D`, each piece gets a `FellowBuoyancy3D` (points a third of the piece apart, at most 16) |

Methods and signals: `FellowWater3D.add_foam(position, radius, amount)`, `get_foam(position)`,
`get_body_foam_stats()`; `FellowBuoyancy3D.is_in_water()`, `water_entered(speed)`, `water_exited`. Measured with
`benchmarks/tools/buoyancy/interaction.gd` (`--frame_work_ms=4`: other work on the main thread after the nodes'
process); pictures with `interaction_demo.gd` (a Vulkan device; `--before`: as milestone 175).

### Water's sound (milestone 177)

Decision 0319: splashes, laps and wakes synthesised from the bubbles they entrain (each at its Minnaert frequency,
3.26 m/s over its radius, damped by its losses, its pitch rising), on the audio thread, no samples. A water nothing
strikes is silent and costs nothing.

| node | property | default | meaning |
|---|---|---|---|
| `FellowWater3D` | `sound_enabled` | true | the water's sound |
| `FellowWater3D` | `sound_volume_db` | 0 dB | its voices' volume |
| `FellowWater3D` | `sound_voices` | 8 | sounds at once (3D players made at the first sound; a splash joins a voice within 2.5 m, else a free one, else the quietest if louder; wakes within 4 m share one; all stop after 10 s of silence) |
| `FellowWater3D` | `sound_max_distance` | 60 m | nothing farther from the listener is played |
| `FellowWater3D` | `sound_unit_size` | 6 m | the voices' `unit_size` |
| `FellowWater3D` | `sound_bus` | Master | their bus |
| `FellowBuoyancy3D` | `sound` | 1.0 | its body's splashes (striking the water from out of it faster than 1 m/s), laps (the water closing over its points faster than 0.15 m/s) and wake; 0: silent |

Methods: `FellowWater3D.play_splash(position, speed, size)`, `add_wake_sound(position, level)`,
`get_sound_stats()`, `debug_render_sound(events, seconds[, rate])`. Measured with
`benchmarks/tools/buoyancy/water_sound.gd` + `water_sound.py` (the synthesis offline; `showreel.wav` to listen to) and
`sound_timing.gd` (Movie Maker; `water_sound.py --timing`).

### The weather's sound (milestone 178)

Decision 0320: rain, wind and thunder synthesised where the listener (the viewport's `AudioListener3D`, else its camera)
is, on the audio thread, no samples; not in the editor.

| node | property | default | meaning |
|---|---|---|---|
| `FellowRain3D` / `FellowWind3D` / `FellowLightning3D` | `sound_enabled` | true | the rain (not snow), the active wind, each strike's thunder heard |
| the same | `sound_volume_db` | 0 dB | its volume |
| the same | `sound_bus` | Master | its bus |
| `FellowWind3D` | `sound_whistle` | 1.0 | the edges' whistling (Aeolian tones at 0.2 × speed / width, from 5 m/s) |
| `FellowWind3D` | `sound_leaves` | 0.5 | the leaves' rustle (0 where there are no trees) |

The rain probes the listener's surroundings every 0.2 s (five rays up: a cover and its height; eight rays down four
metres round: water). The thunder is built on a worker thread at the strike from the bolt's channel (and 3-6 km of it
through the cloud) and heard from its first arrival (distance / 343 s). Methods: `get_sound_stats()`;
`FellowRain3D.debug_render_sound(events, seconds[, rate])`, `FellowWind3D.debug_render_sound(...)`,
`FellowLightning3D.debug_render_thunder(distance, azimuth_degrees, seconds[, rate, seed])`. Measured with
`benchmarks/tools/world_sound/weather_sound.gd` + `weather_sound.py`; a storm with `storm_demo.gd` (Movie Maker).

### Footsteps (milestone 181)

Decision 0323: `FellowFootsteps3D`, last among a skeleton's modifiers, hears a character's feet as drawn and
synthesises their steps on what the ray under each foot finds; placed at the feet; not in the editor.

| property | default | meaning |
|---|---|---|
| `left_foot_bone`, `right_foot_bone`, `left_toe_bone`, `right_toe_bone` | LeftFoot, RightFoot, LeftToes, RightToes | the feet (the toes: the ball; none: the heel alone) |
| `contact_height` | 0.04 m | a foot is down when its lower point is this close to the ground and planted (motion capture's soles: often 0.05) |
| `heel_offset` | 0.05 m | the heel's point behind the ankle |
| `collision_mask` | 1 | what the ray under the foot hits |
| `body_mass` | 75 kg | a step's strength goes with its root |
| `shoe` | Sneaker | sneaker, boot, hard heel, bare feet: the sole's contact, its click, a squeak |
| `default_ground` | Stone | when the collider says nothing |
| `sound_enabled` | true | heard (the `footstep` signal is sent either way) |
| `sound_volume_db`, `sound_bus` | 0 dB, Master | its volume and bus |
| `sound_unit_size` | 3 m | where the level synthesised is heard (a third-person camera's distance) |
| `sound_max_distance` | 30 m | not heard beyond it |

The ground: the collider's `FellowContactSound3D.surface` (or its material: wood, metal, stone, tiles, a mat), a
`fellow_surface` metadata (`"gravel"`, `"snow"`, ... or a number), a `FellowTerrain3D`'s layers by their weights there
(`FellowTerrainLayer.surface`, default dirt), water over the ground. Grounds: stone, wood, metal, tile, carpet, gravel,
sand, snow, grass, leaves, dirt, mud, water. Signal `footstep(foot, position, ground, speed)`; methods `get_ground`,
`get_foot_state`, `get_sound_stats`, static `debug_render`. Measured with
`benchmarks/tools/world_sound/footstep_sound.gd` + `footstep_sound.py`; a CMU walk with
`benchmarks/tools/foot_placement/footsteps_demo.gd`.

### Bodies' contact sound (milestone 180)

Decision 0322: `FellowContactSound3D`, a child of a `RigidBody3D`, makes the body heard from its own vibration (modal
synthesis, no samples) when it strikes, slides or rolls; placed at the body by an `AudioStreamPlayer3D` while it
sounds (at most 24 at once); not in the editor. On a static body it only gives its material to what strikes it.

| property | default | meaning |
|---|---|---|
| `material` | Wood | wood, metal, stone, glass, plastic, rubber, ceramic: the modes' frequencies, their decay, the contact's hardness, the scrape |
| `wall_thickness` | 0 m | a hollow body's wall (a crate's planks, a pipe's, a bottle's); 0: solid |
| `ground_material` | Stone | what it strikes when that has no FellowContactSound3D |
| `mode_count` | 24 | the modes ringing (debris from a `FellowDestructible3D`: 12) |
| `sound_enabled` | true | heard (and the `impact` signal) |
| `sound_volume_db`, `sound_bus` | 0 dB, Master | its volume and bus |
| `sound_unit_size` | 1 m | where the level synthesised (the pressure at a metre; full scale 120 dB SPL) is heard |
| `sound_max_distance` | 40 m | not heard beyond it |

Signal `impact(position, speed, collider)`; methods `get_modes()`, `get_sound_stats()`,
`debug_render(events, seconds[, rate, seed])`. The node raises its body's `max_contacts_reported` to 8. Measured with
`benchmarks/tools/world_sound/impact_sound.gd` + `impact_sound.py`; a yard with `impact_demo.gd` (Movie Maker).

### Fire's sound (milestone 179)

Decision 0321: a fire heard where it burns, synthesised on the audio thread, no samples, each source placed by an
`AudioStreamPlayer3D` its node owns while it sounds; not in the editor.

| node | property | default | meaning |
|---|---|---|---|
| `FellowGasVolume3D` / `FellowBurnable3D` | `sound_enabled` | true | the flames' roar (the gas volume's fire) / the wood's crackle and embers (the burning object) |
| the same | `sound_volume_db` | 0 dB | its volume |
| the same | `sound_bus` | Master | its bus |
| the same | `sound_unit_size` | 3 m / 2 m | the distance heard at the level synthesised (`AudioStreamPlayer3D.unit_size`) |
| the same | `sound_max_distance` | 80 m / 40 m | not heard beyond it (the voice stops) |
| `FellowBurnable3D` | `sound_crackle` | 1.0 | cracks for the area pyrolysing (1: 15 a second a square metre; resinous or damp wood more) |

The flames' heat release comes from the simulated fire (its emission, read back also with `fire_light` off, over
`fire_intensity`: the flames' volume, about 1.2 MW a cubic metre): the roar peaks at about 160 / sqrt(D) Hz, puffs at
1.5 / sqrt(D) Hz, flares when the fire grows fast. Methods: `get_sound_stats()`; `FellowBurnable3D.get_fuel_release()`;
`FellowGasVolume3D.debug_render_sound(keys, seconds[, rate, seed])` (the flames and the crackle offline). Measured with
`benchmarks/tools/world_sound/fire_sound.gd` + `fire_sound.py`; a campfire with `fire_demo.gd` (Movie Maker).

## Rain and wet surfaces (milestone 25)

A `FellowRain3D` node (module `fellow_fluid`; one active at a time) makes it rain around the camera and wets the
scene where the rain falls (Forward+):

* **drops**: GPU particles in a box of `area` around the camera (`max_drops`, `drop_speed`, `wind`, `drop_size`,
  `drop_color`), stopping on the scene as seen from above and splashing there (`splashes`); `rain` (0-1) scales them;
* **wet materials**: every opaque material, after its own code, by `wetness`: porous ones (rough, not metallic)
  darker, all with a glossy film; **puddles** on flat rough ground (`puddles`) with the rain's **ripples**
  (`ripple_tile`, `ripple_strength`), in the depth pre-pass too so screen-space reflections show them;
  `wetness_follows_rain` with `wetting_time` / `drying_time` to wet and dry over time;
* **shelter**: a heightfield of the scene seen from above following the camera (`occlusion_size`,
  `occlusion_height`, `occlusion_resolution`; `occlusion_always_update` for moving roofs; `get_occlusion()` for its
  `heightfield_mask`) keeps the drops off and the ground dry under roofs;
* a `FellowWater3D` shows the ripples too;
* **rainbows** (milestone 34, decision 0134): while it rains or lately rained (fading over 90 s once it stops; none
  once it snows) and the sun shines on the rain, the primary and secondary bows around the point opposite the sun,
  over the sky and over what stands behind the rain (`rainbow` scales them, 0: none);
* **rain shafts** (milestone 35, decision 0135): with the volumetric clouds on, grey curtains of rain below their dense
  parts, from 2 km to 40 km away (`shafts` scales them, 0: none);
* **ice and hoarfrost** (milestone 36, decision 0136): `freeze` (0-1) freezes the water on the ground — no ripples, the
  puddles dark clear ice with white rims, the film a glaze; `frost` (0-1) covers what faces the sky with hoarfrost,
  fine grey-white glinting grains, under the snow; a FellowWeather3D sets both from its temperature
  (`freezing_time`);
* **showers** (milestone 37, decision 0137): `showers` (0-1, 0 by default) makes it rain at the camera only as the
  volumetric clouds above let it — under their dense parts, as the rain shafts hang — eased over 2 s as showers pass
  with the clouds' wind (`get_rain_here()`); the drops, ripples, wetting and settling snow follow it;
* **drying in patches** (milestone 38, decision 0138): between dry and wet the surfaces are wet in patches — the high
  spots drying first and wetting last — and the puddles shrink to their deepest instead of fading (`drying_patches`
  1; 0: an even wetness);
* **wind-driven rain** (milestone 39, decision 0139): in a wind the shelter follows the drops' slant — walls facing
  the wind get wet under their eaves and as wet as the ground, the lee of a building stays drier (`wind_driven` 1;
  nothing below a breeze of about 1.8 m/s);
* **slush** (milestone 40, decision 0140): `slush` (0-1) soaks the snow cover with its meltwater — greyer, glossier,
  no glints, its thin parts first; a FellowWeather3D sets it while the snow melts;
* **thin streaks** (milestone 41, decision 0141): `streak_width` (m, 0: off) draws each drop at its own width or a
  pixel, its opacity scaled to match — thin near, faint far, instead of `drop_size`'s bars.

Cost (RTX 5070 Ti Laptop, 1080p, Rain Lab): 0.36 ms of GPU time raining — screen-space reflections 0.13 ms more
(wet surfaces are glossy enough for SSR; without SSR there is none of it), the wetting 0.12 ms (depth pre-pass and
opaque pass), the ripples 0.04 ms, the particles 0.04 ms, the drops' drawing 0.03 ms; a FellowRain3D without rain and
wetness costs nothing. Transparent materials are not wetted. `rendering/fellow/fluid/enabled` off turns the wetting
off. Decision record 0124; bench Rain Lab (`--scene=rain`, `--rain=on|dry|off`, `--rain_light=day|dusk`).

## Wind (milestone 26)

A `FellowWind3D` node (module `fellow_fluid`; one active at a time) is the scene's wind, blowing along the node's
forward axis at `speed`, with gusts (`gustiness`, `gust_size`) that roll over the landscape with it. Vegetation moves
with it in the vertex shader (Forward+):

* a `BaseMaterial3D`'s **Wind** group: `wind_height` (the plant's height in its mesh, from y = 0), `wind_bend` (how
  far it bends in 10 m/s: grass about 1, bushes 0.3, trees 0.05-0.1), `wind_flutter` (leaves 1, bark 0), `wind_sheen`
  (bent grass brightens: the gusts' waves; about 1 for grass);
* a `ShaderMaterial`: `render_mode fellow_wind;` and `WIND_HEIGHT`, `WIND_BEND`, `WIND_FLUTTER`, `WIND_SHEEN` in its
  `vertex()`.

Each mesh instance (a MultiMesh's instance or a node) is one plant standing at its origin: it bends with the force of
the wind there, sways at a frequency of its size, its branches at their own phases, its leaves flutter; normals turn
with the bend; shadows and motion vectors follow. A `FellowRain3D` takes its drops' wind from it
(`wind_from_scene`). Not moved: the Mobile and Compatibility renderers, SDFGI's voxelization, `skip_vertex_transform`.
MultiMesh culling knows the wind's bound (no cull margin needed); the shadows of plants in the wind are cached from
directional cascade `shadows/wind_cached_from_cascade` on (2; -1: never — every cascade redrawn every frame), where
they keep the pose of the cache's last build (RTX, Wind Lab: −0.32 ms from the third cascade; from the second
another −0.11 ms, but only the nearest cascade's shadows sway).

Cost (RTX 5070 Ti Laptop, 1080p, Wind Lab): 0.56 ms of GPU time against the still scene (the near shadow cascades
0.33 ms, the opaque pass's two poses per vertex for the motion vectors 0.23 ms); the TIME-based sway projects write
costs 1.81 ms. Decision record 0125; bench Wind Lab (`--scene=wind`, `--wind=on|off|shader`).

**The scene's wind for water, gas and particles (milestone 27).** The same wind moves more than plants, each when
asked:

* `FellowWater3D.wind_from_scene`: the spectrum follows the wind's speed and direction (rebuilt when they change by
  2 % or a degree), and the gusts roughen the shortest waves — darker, rougher water in a gust, a mirror in a lull,
  the "cat's paws" running over a lake (`gust_ripples`, 1, scales them);
* `FellowGasVolume3D.sleep_distance` (milestone 68, 0: never): while the camera is farther from the box, the gas is not
  stepped (its state kept, still drawn) and the burning objects in it wait; `is_sleeping()`.
* `FellowGasVolume3D.wind_from_scene`: the gas's `wind` each step is the scene's wind at the box's centre, gusts
  included (flames and smoke lean and flicker with the gusts);
* a `ParticleProcessMaterial`'s **Wind** group, `wind_enabled` and `wind_influence` (1/s): the particles' horizontal
  velocity relaxes towards the wind at their position, gusts included (sparks, embers, leaves, snow); a particle
  shader's process function reads it as `WIND_VELOCITY` (zero without a FellowWind3D, and on the Compatibility
  renderer).

Particles that drift need a visibility box reaching as far as they drift (downwind). Cost (RTX 5070 Ti Laptop,
1080p, `m27_f287`, `m28_f288`): the water's gust ripples and the gas's wind not measurable; Particles Lab's update
+0.04 to +0.15 ms (of 0.5 ms), its frame +0.09 ms. Decision record 0127; FellowBench `--weather_wind=<m/s>` in any scene, `--wind_leaves=<n>` falling
leaves in Wind Lab.

## Snow (milestone 28)

`FellowRain3D` snows with `precipitation` = Snow (Forward+):

* **flakes** (`max_drops`, scaled by `rain`): soft camera-facing discs (`flake_size` 1.8 cm, `flake_color`) falling at
  `flake_speed` (1.1 m/s) and swinging about their paths (`flake_flutter`, 12 cm), in a box of `area` around the
  camera (8 s lives, fading in and out); with a `FellowWind3D` they take the wind where they are, gusts included
  (`flake_wind_influence`, 1.5/s); they stop on the shelter heightfield (none under roofs) and receive shadows; drawn
  in the opaque pass with hashed alpha, with the particles' motion vectors (blended, TAA wiped them out);
* **snow cover** (`snow`, 0-1, independent of what falls): what faces up is covered where the snow falls — patchy
  while thin, everything facing up once deep, nothing on slopes steeper than 60°, bare under roofs and trees (the
  shelter, eroded by 35 cm: wires and lamps overhead leave no bare spots); fresh snow is white, rough and smooth with gentle undulations; `snow_sparkle` glints where a crystal
  faces the light; `snow_follows_precipitation` grows it in `snowing_time` (900 s) and melts it in `melting_time`
  (1800 s);
* no ripples on puddles or water, no splashes, while it snows.
* **tracks** (milestone 30; `tracks`, on): `FellowSnowTracker3D` nodes (feet, wheels, runners: `radius`, `length`,
  `depth`, `contact_height`) press capsules into the snow while they touch the scene's top, and `stamp(from, to,
  radius, depth)` presses one when asked; the cover shows the tracks' walls, packed snow at their bottom, the ground
  where they reach `snow_depth` (0.08 m), a rim beside them; the falling snow fills them back (`track_refill_time`,
  900 s). They live in a world-anchored window of `track_window` (48 m, `track_resolution` 2048) around the camera.
  Cost: not measurable (Rain Lab, a cart's wheels pressing every frame, `m30_f292`). Decision record 0130.

Cost (RTX 5070 Ti Laptop, 1080p, Rain Lab, `m28_f289`): 0.37 ms of GPU time snowing on a covered street (60000
flakes; raining 0.34 ms) — the depth pre-pass +0.10 ms and the opaque pass +0.10 ms (the flakes and the cover), the
flakes' update +0.07 ms. Decision record 0128; FellowBench `--weather_snow=<0-1>` in any scene (`--weather_snow_cover`,
`--weather_snow_flakes`, `--weather_snow_area`, `--weather_snow_sparkle`), Rain Lab `--rain=snow`.

## Lightning (milestone 29)

A `FellowLightning3D` node (module `fellow_fluid`) strikes at random (`strikes_per_minute`, 4; `distance_range` 0.4-3 km
around the camera) or when `strike(distance, azimuth)` is called. A strike is two to four return strokes 40-120 ms
apart, each a flash decaying over about 40 ms:

* a directional light from the bolt's direction (`energy` 6 at 1 km, `color`; `shadows`), visible only during a
  flash, no sun disc in the sky;
* the bolt: a branching channel from the cloud (`cloud_height`, 1.2 km), ribbons of a constant width on screen
  (`bolt_width`, 0.006 rad), additive and unshaded (`bolt_brightness`; bloom makes the glow), hidden by the scene in
  front of it; a lit cloud above it;
* the same clock (`time`, `seed`) gives the same storm; `struck(position, distance)` for the thunder
  (distance / 343 s later).

Cost (RTX 5070 Ti Laptop, 1080p, Rain Lab at dusk, `m29_f290`): +0.04 ms of GPU time on average with a strike every
2 s (the flash's shadows +0.02 ms, the opaque pass +0.03 ms); nothing between strikes. Decision record 0129; FellowBench `--weather_lightning=<per minute>` in any scene
(`--weather_lightning_flash`, `--weather_lightning_frames`).

## Weather (milestone 32)

A `FellowWeather3D` node (module `fellow_fluid`) sets the weather and changes it over time: `weather` = Clear,
Cloudy, Overcast, Rain, Storm, Snow, Blizzard (presets) or Custom, `set_weather(weather, seconds)` (`transition_time`
30 s when none is given). The weather is a state — `wind_speed`, `gustiness`, `precipitation` (none, rain, snow),
`intensity`, `lightning_rate`, `cloud_coverage`, `cloud_density`, `sunlight`, `ambient_light` (the sky's light),
`fog_density`, `temperature` — eased from where it is; rain and snow hand over at the transition's midpoint, when
nothing falls. It drives (`controls`, all by default): the scene's `FellowWind3D`, `FellowRain3D` and
`FellowLightning3D` (found, or created when needed: `create_missing`, a created rain node with thin streaks; all at the
start: `prepare_nodes`); the
volumetric clouds' coverage and density in place of the project settings (the clouds must be on), and so their
shadows (decision 0133); the sun on the scene's surfaces only where the clouds cast no shadows (its light buffer
energy times `sunlight` — the sky and the clouds keep the full sun); the
environment's fog (added to its own, in `fog_color` as bright as the sky; milestone 211: a depth fog of its own drawn
in instead — its end no further than the visibility, 3 / `fog_density`, its begin a twentieth of that at most, its
colour turning to `fog_color`, the sky veiled) and its background energy (the sky, its ambient light and
reflections); milestone 211: the sun's share on FellowEngine's smoke, ray-traced bounces and screen gather as on the
surfaces; and the ground — wet in the rain, drying after it, covered by falling snow below 1 °C,
melting above 0 °C, its water freezing and frosted below 0 °C when nothing falls (decision 0136) (`ground_speed`
speeds it up; `settle_ground` starts it as the weather leaves it). It does nothing
in the editor. Without the node nothing changes (bit for bit); with it, the cost is that of the parts it turns on.
Decision 0132.

## Neural motion (milestone 43)

Module `fellow_motion`. `FellowMotionModel` runs NVIDIA's MotionBricks SOMA model on a RenderingDevice of its own
(`load(path, max_batch)`, `run(inputs)` with the ONNX model's inputs and outputs, `get_info()`, `get_last_timings()`):
an `.fmb` file that `modules/fellow_motion/tools/fmb_convert.py` writes from the released checkpoints (the weights are
NVIDIA's, under the NVIDIA Open Model Agreement, and not in this repository). `cooperative_matrix` (on): the products
on tensor cores (`VK_KHR_cooperative_matrix`, fp16 weights, a fixed subgroup size), fp32 elsewhere; `profile` (GPU
timestamps per op); `target_groups` (96, split-K). `FellowMotionCharacter3D` (`model_path`, `body_path` — the SOMA
mannequin `tools/fmb_usd.py` writes — and `desired_velocity` each frame) walks, jogs, runs, turns and stops by itself
with no animation clip: `max_speed` (3.5 m/s), `lead_time` (0.97 s), `speed_halflife` (0.1 s), `catch_up` (on),
`get_up` (on), `transition_early_frames` (8), `min_tokens` / `max_tokens` (8 / 14), `crossfade_time` (0.15 s),
`inference_mode` (async, flush, immediate), `max_batch` (32), `idle_motion` (on), `body_color`; `get_stats()`, the
static `get_service_stats(model_path)` and `flush_requests(model_path)`. Nothing changes without the nodes. Decision
0143; bench Motion Lab (`--scene=motion`, `--motion_model=<.fmb>`, `--motion_body=<.glb>` or the environment's
`FELLOW_MOTION_MODEL` and `FELLOW_MOTION_BODY`, `--motion_crowd=16`, `--motion_mode=flush|async|immediate`,
`--motion_rewind=1`).

Milestone 44 (decisions 0144, 0145): the service encodes the requests and decodes and endpoint-corrects the results
on the WorkerThreadPool (`get_service_stats()` reports `avg_decode_msec`), and a character's frame poses only the 26
joints the model drives. On tensor cores with a subgroup of 32 or less the products load the activations four at a
time; the environment's `FELLOW_MOTION_GEMM=scalar` (a diagnostic) takes milestone 43's scalar loads — the same
results either way.

## Texture streaming (milestone 45)

`rendering/fellow/streaming/textures` (off; read at start-up): a `CompressedTexture2D` holding a VRAM-compressed
image with its full mip chain (how Godot imports 3D textures) loads only its mips at or below
`texture_resident_size` (128 px); the RenderingServer loads the rest on worker threads as the cameras need them and
releases them when they no longer do; from milestone 139 (decision 0264) a 2D DDS file of a compressed format with its
full mip chain loaded directly (as Bistro's) too. The level each texture needs comes from the instances that use it (their box's
nearest view depth, their smallest scale, their surfaces' texel density, BaseMaterial3D's `uv1_scale`) and the
samplers' bias; `texture_lod_bias` (-1) is the margin, `texture_budget_mb` (0: none) caps the streamed textures'
memory (every texture coarsened by the same bias over it; milestone 113, decision 0223: `texture_budget_from_used_level`
(on) biases the level each texture would use, never finer than its first, not its estimate under magnification).
Milestone 113 (decision 0222): `texture_swaps_keep_shadows` (on, exact): a swap does not invalidate the directional
shadow caches through materials whose shadows cannot read their textures. Milestone 114 (decision 0225):
`rendering/fellow/frame/transfer_staging_trim` (off since milestone 117, decision 0233: a release emptying a 256 MiB block
of the memory allocator frees it in about 30 ms, holding up the frame's submission): RenderingDevice's transfer workers
release a staging buffer above 4 MiB after `transfer_staging_trim_idle_s` (5) unused, all but the largest (host memory,
counted in the driver's video memory: the streamer's loads made the workers multiply, each keeping its largest
upload's). FellowEngine's statistics count the driver's device memory allocations and frees (`memory_allocations`,
`memory_allocation_bytes`, `_usec`, `_max_usec`, `memory_frees`, `memory_free_bytes`, `memory_free_usec`; Vulkan). Milestone
118 (decision 0234): `rendering/fellow/frame/transfer_tight_staging` (on; read when the device is created): a
block-compressed upload reserves staging for its rows of blocks, as copied (stock reserved its rows of pixels: 4 times the
data with 4x4 blocks) — Sponza streamed's workers end with 160-192 MiB instead of 512, the same copies and pictures;
`texture_get_data_async()`'s download staging is reserved the same way. Milestone 119 (decision 0236, with FellowEngine
on, a correctness fix): `buffer_get_data_async()` and `texture_get_data_async()` return every byte — a texture that is
not square its last mips (stock left them as they were), a download larger than what the download staging ring has left
the parts copied before the ring's wait (stock dropped them). Milestone 115 (decision
0227, milestone 116 on the RTX): `rendering/fellow/frame/transfer_large_own_submit` (on): an upload above 4 MiB made off
the render thread submits and waits for its worker's earlier work and its own copy itself, so that no frame waits for
a large copy (FellowEngine's statistics: `transfer_frame_large_submits`, the frame's own copies over 4 MiB;
`transfer_submit_waits`, `transfer_submit_wait_usec`, the frame's end waiting for a held worker).
Milestone 120 (decision 0240): with FellowEngine on, the device is created with `VK_EXT_memory_budget` where the
driver has it and the memory allocator reads the operating system's budget for the process's video memory every frame
(FellowEngine's statistics: `memory_budget_bytes`, `memory_budget_usage_bytes`, `memory_budget_from_driver`);
`texture_budget_auto` (0, off; a share): the streamed textures' budget is that share of the driver's budget less what the
process uses besides them (at least 64 MiB, in 32 MiB steps, capped by `texture_budget_mb`), reported as
`budget_bytes` and `auto_budget_bytes` by `RenderingServer.fellow_get_texture_streaming_info()`.
`texture_instances_per_frame` (2048) instances are
evaluated a frame; `texture_max_jobs` (4), `texture_upload_mb_per_frame` (64), `texture_release_hold_s` (2),
`texture_unreferenced_hold_s` (5); `texture_info_details` lists every texture in
`RenderingServer.fellow_get_texture_streaming_info()`, which reports the streamer's state (resident, target, wanted and
full bytes, pending, loads, releases, `settled`). `terrain_region_uploads` (on, exact; milestone 61, decision 0168):
`FellowTerrain3D`'s sculpting, painting and blits upload only the texels they changed and the mips over them. Loaded, the same images as without it (lavapipe, bit for bit);
until the loads land, coarser mips. Not streamed: lossless or uncompressed textures, DDS (`ImageTexture`), texture
arrays and cubemaps; a texture drawn in 2D, in a decal or area light atlas, or by a material that is not a
BaseMaterial3D-style 3D shader (sky, canvas, fog, particles, custom shaders) stays at full resolution; one not used
yet keeps its small mips. Decision 0146; bench Streaming Lab
(`--scene=streaming`, textures from `benchmarks/scripts/make_streaming_textures.py`).

## World streaming (milestone 46)

Module `fellow_world`. `FellowStreamingCell3D` (`scene_path`, `bounds`, `load_distance`; its own children are a proxy
shown while unloaded) below a `FellowWorldStreamer3D` (`load_distance` 200 m, `unload_margin` 25 m, `max_loading` 2,
`max_activations_per_frame` 1, `max_releases_per_frame` 2, `threaded_instancing` on, `streaming` on, `use_camera`
on, `targets`; `prefetch(position, radius)`, `load_now()`, `is_settled()`, `get_stats()`): each cell's PackedScene
loaded on the ResourceLoader's threads when the camera or a target comes within its distance, instantiated on a
worker thread, added to the tree a few a frame and freed beyond the distance plus the margin. Nothing changes without
the nodes. Decision 0147; bench World Lab (`--scene=world --world_streaming=1`).

Milestone 50: `split_into_cells(root, cell_size, directory, options)` cuts a level into cells (the root's 3D
children grouped on the streamer's XZ grid, each group saved as `cell_X_Z.scn` and replaced by a cell; options
`exclude_classes`, `max_span` 2, `load_distance`, `extension`, `owner`); the editor runs it from Project > Tools.
`editor_preview` (on): in the editor the cells show their scenes (internal, not saved). Decision 0151.

Milestone 66 (decision 0174), **proxies and streamed meshes**: `bake_proxies(directory, options)` bakes a proxy for each
cell — its meshes, MultiMesh instances (up to 256 a node) and terrain regions (`FellowTerrain3D.make_proxy_arrays(quads)`)
merged, simplified (meshoptimizer) and coloured by their materials' mean albedo per vertex — saved as `<cell>_proxy.res`
and added as the cell's `FellowProxy` child (shown while unloaded; hidden in the editor while the cell's scene is
shown). Options `ratio` 0.05, `terrain_quads` 32, `proxy_distance` 0 (always drawn), `proxy_shadows` off, `owner`;
`stream_meshes` (off): the cells' meshes of at least `mesh_min_triangles` 2000 triangles replaced in their scenes by
stand-ins of `mesh_ratio` 0.15 of their triangles, the full meshes saved apart (`meshes/`) and loaded on the
ResourceLoader's threads within the streamer's `mesh_distance` (0: whenever the cell is loaded; released beyond it
plus `unload_margin`), `max_mesh_loads` 2 at once; milestone 141 (decision 0266): `mesh_middle_ratio` (0: none) saves
a middle level of each (one per full mesh's file), shown between `mesh_distance` and the streamer's
`mesh_middle_distance` (0: none). The editor runs it from Project > Tools > **Bake Streaming Proxies**. Bench
`bench/world_proxies_check.gd`.

Milestone 127 (decision 0251), **textured proxies**: a proxy's shape is the cell welded by position and simplified by
meshoptimizer directly (no border locked, nothing pruned: small buildings kept), its normals rebuilt with a 60° split.
Where a cell's materials have albedo textures, in editor builds, `bake_proxies` unwraps the proxy (xatlas) and bakes a
texture of what the cell draws under each texel — material colour × vertex colours × albedo texture at the level of the
texel's size, past alpha-tested holes — BC1 with mipmaps in the proxy's file, a material per proxy. Options
`texel_size` 0.5 m (0: vertex colours), `max_texture_size` 512 (coarser texels beyond). Otherwise (and for terrain
regions) vertex colours, each corner from the surface under it. The result has `textured`, `texture_bytes`,
`texture_ms`, `texture_hit_share`. The editor's dialog has the texel. Bench `bench/proxy_texture_check.gd`.
Milestone 138 (decision 0263), **proxies' textures in files of their own**: `bake_proxies` option `texture_files` true
(false: inside the proxy's file, as before): each textured proxy's, group's and level's texture is saved beside it as
`<proxy>_albedo.ctex`, a `CompressedTexture2D` the proxy's material refers to — with `streaming/textures` only the
levels a camera needs are resident; without it the same texels load whole. The result has `texture_files`,
`texture_file_bytes`.

Milestone 128 (decision 0252), **grouped proxies**: `bake_proxies` option `group_cells` 4 (0: none) groups the cells
4 × 4 on a grid of their boxes' centres; each group gets one proxy of its cells' content together (its cells' proxies'
triangles over the square of how much farther it is seen, coarser texels), a `FellowGroupProxy_<x>_<z>` child of the
streamer drawn beyond `group_distance` (0: where none of its cells can be loaded) as its cells' proxies' visibility
parent, and hidden while any of its cells is loaded. The result has `groups`, `group_triangles`,
`group_texture_bytes`; the editor's dialog has the group size. World Lab: 347 → 107 draws. Milestone 132 (decision
0256): option `group_fade` (0 m): the cells' proxies fade over that distance on each side of their group's switch.
Milestone 137 (decision 0262), **levels of groups**: option `group_levels` 1 (at most 4): above 1, the groups' proxies
are grouped `group_cells` × `group_cells` again, level by level, each set of two or more with one coarser proxy baked
from the proxies below it (`FellowGroupProxy_L<n>_<x>_<z>`, `group_l<n>_<x>_<z>_proxy.res`), drawn from where all of
them would be drawn, their visibility parent (`group_fade` fading them into it), hidden while any of them is; its
objects simplified to `terrain_error` pixels there at least (2 when that is 0), its texture up to twice as wide a level
(at most 4 096). The result's `group_levels` lists each level's `groups`, `triangles`, `texture_bytes`; the editor's
dialog has the levels. Frontier Lab `--frontier_group_levels=<n>`.

Milestone 134 (decision 0258), **cells crossing over**: `FellowWorldStreamer3D.crossfade` (seconds, 0: a switch): a
cell's content enters keeping none of its pixels and its proxy stays; over the time the content's share grows and the
proxy keeps the others (dithered, as decision 0256's fades: TAA or FSR resolve it), then the proxy hides. Unloading,
the content stays as a ghost fading out while the proxy fades in, then is freed. `is_settled()` waits for the fades.
Shadows switch (from milestone 135, decision 0260: they cross over too, dithered in the shadow maps); terrain regions
take part (`FellowTerrain3D.fellow_set_dither_alpha()`). A streamed mesh's swap between its stand-in and its full mesh
(`mesh_distance`) crosses over the same way, the outgoing mesh drawn by an instance of its own meanwhile (decision
0259; from milestone 135 casting the node's shadow, the two shadows dithered into one). The renderer's
`fellow_instance_set_dither_alpha()`. Frontier Lab `--frontier_crossfade=<s>`.

Milestone 131 (decision 0255), **terrain proxies by error**: `bake_proxies` option `terrain_error` 2 (pixels; 0: the
terrain simplified with the rest, to `ratio`, as before): a cell's terrain is simplified apart from its other content,
its region's edges kept, as far as an error of that many pixels allows in a view of 1 080 lines with a 75° field of
view at the distance at which the proxy is first drawn (its cell's load distance and `unload_margin`; a group's
switch) — its count whatever that takes. A ratio's count left a region's proxy a fan from its locked edges across the
valleys. The result has `terrain_triangles`, `group_terrain_triangles`; the editor's dialog has the error. Bake proxies
with the load distances the streamer will use: the error and the groups' switch are computed from them.

## FSR 3.1 upscaler (milestone 47)

`rendering/fellow/upscaling/fsr3` (on from milestone 49; read every frame): a viewport in Godot's FSR 2 scaling mode
(`scaling_3d_mode` FSR 2, any `scaling_3d_scale`, 1.0 for native AA) runs AMD's FidelityFX Super Resolution 3.1
upscaler instead of FSR 2.2, with the same inputs, jitter, sharpness (`fsr_sharpness`) and output; switching makes a
new context (a new history). `fsr3_half_precision` (on; read at start-up): half precision in the passes that have it,
where the GPU supports it (not SPD's pyramids, not the accumulation on NVIDIA: no faster there in half precision,
decision 0224). The SDK's tunables:
`fsr3_velocity_factor` (1; 0 steadies bright pixels), `fsr3_reactiveness_scale` (1), `fsr3_shading_change_scale` (1),
`fsr3_accumulation_added_per_frame` (0.333; less ghosts less where the history is rejected),
`fsr3_min_disocclusion_accumulation` (−0.333). `fsr3_debug_view` draws the SDK's debug view over the output. In motion
less error and flicker than FSR 2.2 in every mode measured; on still views at 2× softer. On the RTX it costs what FSR
2.2 costs (within 0.7 % at 1080p). Decision 0148; bench `tools/lavapipe/upscaler_check.sh`.

## Motion matching (milestone 48)

Module `fellow_animation`. `FellowMotionDatabase.build(skeleton, animations, options)` turns clips into a searchable
database (60 Hz; bone roles by name, feature weights, contact thresholds in `options`; saved as a resource;
`import_bvh()`/`create_skeleton()` read BVH motion capture). `FellowMotionMatcher3D`, a modifier of the skeleton:
`desired_velocity`, `desired_facing`, `search_interval` (0.1 s), `inertialization` (on, `inertialization_halflife`
0.1 s), `velocity_halflife`/`rotation_halflife` (0.27 s), `synchronization` (on, `adjustment_halflife` 0.1 s,
`clamp_distance` 0.15 m), `foot_locking` and `foot_ik` (on, `unlock_radius` 0.2 m), `lod_distances` (none),
`root_path` (the skeleton's parent), `auto_update` (on; `advance()`, `advance_group()` for many on the worker threads);
`get_stats()`, `get_global_stats()`, `get_last_search()`. Nothing changes without the nodes. Decision 0149; bench
Matching Lab (`--scene=matching`, data from `benchmarks/scripts/get_cmu_locomotion.sh`, `bench/matching_eval.gd`).

Milestone 56, off by default: `ground_adaptation` (rays down under the root and each foot's heel and toe against
`ground_collision_mask`, within `ground_probe_height` 0.6 m: the root follows the ground, the feet are placed on it,
the hips lowered for the lower foot, `ground_alignment` turns a foot to the slope, `ground_halflife` 0.05 s;
`ground_foot_placement` off: the root alone); the database's `mirror` (each clip mirrored, its left and right bones
swapped; pairs by name or `mirror_pairs`) and `tags` (a clip's name to its tags, `"mirrored"` on mirrored clips) build
options, the matcher's `tags_required` and `tags_excluded`. Decision 0157; `bench/ground_check.gd`,
`bench/matching_tags_check.gd`, Matching Lab's `--matching_mirror=1`.

Milestone 164, on by default (`foot_ik` needed for the last two): `transition_cost_bias` (2.0: the playing pose
continues unless a frame is cheaper by this much) and `transition_jump_threshold` (0.25 s: the playing clip's nearby
frames are not candidates); `stride_warping` (on, `stride_warping_limits` 0.75-1.35: each foot's place along the way
scaled about the hips by the character's speed over its animation's, the hips lowered to reach); `lean_amount` (1.0,
`lean_max_angle` 20°: the body turned about the ground under the hips into the acceleration the character has beyond
its animation's, `atan(a / g)`). `get_stats()` adds the lean's statistics against balance's and the stride's ratio.
Decision 0305; Matching Lab's `--matching_bias`, `--matching_jump`, `--matching_stride`, `--matching_lean` (also
`bench/ground_check.gd`), `bench/matching_video.gd` films a character.

Milestone 165, on by default: `orientation_warping` (`orientation_warping_max_angle` 90°: the hips turned about the
vertical from the clip's way to the simulation's, the spine turned back so that the chest keeps the facing, the root
motion turned with them). Decision 0306; Matching Lab's `--matching_orient`, `--matching_orient_max`,
`--matching_strafe=1` (walkers face the centre), `matching_video.gd --follow=walker`.

Milestone 166: `FellowPhysicalAnimation3D`, a modifier after the animating ones (the skeleton's modifiers in the
physics step): the bones as bodies built at run time on a `PhysicalBoneSimulator3D` it adds (`min_bone_length` 0.08 m,
`total_mass` 70 kg, `collision_layer` 16, `collision_mask` 1), driven each step towards the animation (`position_gain`,
`rotation_gain` 0.5; `strength` 1, its cube the step's share; pins while driven, cones of `swing_limit` 50° and
`twist_limit` 30° once under 0.5); `apply_hit(position, impulse)` weakens the struck body by `hit_weakening` 0.85,
recovering with `recovery_halflife` 0.3 s; `strength` 0 is a ragdoll (Jolt Physics); `simulate`, `build()`,
`reset_bodies()`, `get_stats()`. Decision 0307; `bench/physical_check.gd`, `bench/physical_video.gd`.

Milestone 183, opt-in: `balance` (off) with `motion_matcher_path` (the `FellowMotionMatcher3D` it steers): a hit's
capture point (the centre of mass's offset from the animation's plus the hit's velocity for the whole body over
√(g / h)) under `step_threshold` (0.12 m) is taken back where it stands; beyond it the motion matcher is pushed that
way (`FellowMotionMatcher3D.push(velocity, duration)`, `is_pushed()`: a stagger); beyond `fall_threshold` (0.7 m) the
body falls, stiff about its feet until `topple_angle` (40°), then limp. Lying still, after `get_up_delay` (1 s, with
`auto_get_up`; or `get_up()`), it plays the motion matcher's database's clip tagged `get_up_tag` ("get_up"; left out of
its search) that starts on the face it lies on and fits it best, blended from where it lies over `get_up_blend`
(0.5 s), and walks on. `knock_down(velocity)`, `get_state()` (`STATE_STANDING`, `_STAGGERING`, `_FALLING`, `_DOWN`,
`_GETTING_UP`), `get_landed_face()` (`FACE_UP`, `_DOWN`, `_SIDE`), `get_balance_stats()`; the signals `staggered`,
`fell`, `landed`, `got_up`. With balance a hit is spread over the body by mass. Decision 0325; `bench/balance_check.gd`,
`bench/balance_video.gd` (CMU subjects 139 and 140: `benchmarks/scripts/get_cmu_locomotion.sh`).

Milestone 184 (decision 0326), with balance: a fall buckles where the motion matcher's database knows the legs — after
0.12 s the hips sink along the line from the feet (up to 2 m/s) as it leans about them, the knees bent forward over the
feet by IK, the arms reaching towards the ground — and goes limp with the hips 0.38 m up, the knees at the ground or
past `topple_angle` (now 60°); without the legs' roles, the stiff fall. `get_balance_stats()` adds `fall_buckles`,
`fall_limp_seconds`, `fall_limp_hips_m`, `fall_limp_lean_deg`, `fall_kneeling`. Getting up hands the clip's last frame
to the motion matcher where the clip is played (`FellowMotionMatcher3D.play_from(frame, position, facing)`, new: the
frame shown as it is, the next search by the trajectory alone). `FellowMotionMatcher3D.foot_step_lift` (on, 0.08 m; 0:
milestone 183's feet): a foot lock let go of on the ground waits until the foot has stepped where its animation is,
lifted on an arc; a foot let go of as it lifts gets the arc on top; a foot dragged low while the character stands is
lifted by its speed; `get_stats()`'s `foot_slide_raised` (stepping). Matching Lab's `--matching_step_lift`.

Milestone 185 (decision 0327), on by default: `FellowPhysicalAnimation3D.anatomical_limits` — the bodies a person's,
found from the skeleton (the trunk, the limbs off it, each limb's two longest neighbouring bodies its upper and lower
part, a limb below the root a leg): masses shared out as people's (de Leva 1996), the feet soles (boxes from the heel
to past the ball), and limp, six-degree joints as each part bends — the spine 12° a segment, the neck 30°, a hip flexed
−15°…120° from the thigh's rest, a knee and an elbow hinges −3°…145°, the girdle's bones 6°, with joint friction; off:
milestone 166's cones (`swing_limit`, `twist_limit`) and masses by volume. `get_joint_angles()`; `get_stats()`'s
`limp`. `FellowMotionMatcher3D.foot_grounding` (on): a foot in contact lowered (or raised) so that its ankle or toe is
as high as the database's are when down (their 5th percentile in contact); `get_stats()`'s `ground_shifts_mm`.
Matching Lab's `--matching_grounding` and its planted toes' heights; its mannequin's feet got soles.

Milestone 186 (decision 0328), on by default: `FellowPhysicalAnimation3D.fall_step_time` (0.4 s; 0: milestone 185's
fall where it stands) — with balance and a motion matcher, a hit whose capture point is beyond `fall_threshold` but
within twice it first pushes the matcher that way (ω c, at most 2.5 m/s, decaying over twice this time; `staggered` is
emitted) and the fall begins after this long from where the step took it; as the legs give way each foot turns flat
about its ankle and comes down to where the lower one stands (0.15 s). `get_balance_stats()`'s `fall_steps`.
`FellowPhysicalAnimation3D.stuck_rescue` (on): a body held at full strength more than 5 cm from its place without
coming nearer (under 0.1 m/s towards it) for 0.3 s is put there if its shapes, 2 cm higher, meet nothing but the
character's own bodies — while the animation is followed (with balance: standing or staggering); `get_stats()`'s
`stuck_rescues`. `balance_check.gd`'s `--fall_step`, `--stuck_rescue`.

Milestone 187 (decision 0329), on by default: `FellowMotionMatcher3D.foot_skate_guard` (with `foot_step_lift` above 0)
— a foot in contact that no lock holds (stepping to where its animation went, carried by a transition's blend) is
lifted by its speed over the ground (0.12 s of it, at most half of `foot_step_lift`, its step's arc counted), whether
the character stands or not, and `foot_grounding` keeps a step's arc over the ground; off: milestone 186's feet.
`get_stats()`'s `foot_skim_distance`, `foot_air_time`. `balance_check.gd`'s `--skate_guard` (and the feet's skating),
`balance_video.gd`'s `--program=staggers`, Matching Lab's `--matching_skate_guard` (and the feet's skimming).

Milestone 208 (decision 0350): `FellowPhysicalAnimation3D.get_up_from_animation(blend = -1)` — lying (down, or limp
after a fall), the bodies blended from where they lie into the animation the skeleton is given from then on (the game's
own get-up clip, the character placed where the body lies) over `blend` seconds (`get_up_blend` if negative), then
standing and `got_up`; no motion matcher needed (`get_balance_stats()`'s `get_up_match_error_m`: the clip's first pose
from the bodies, by mass; `get_up_blend_speed_max_m_s`). `knock_down()` goes on to lying with `balance` off. A root
bone on the ground under the hips with one child (a game rig's root) gets no body. The rooftops showcase's people:
`rooftops_crowd.gd`'s `bump()` (a shove under `NUDGE_MAX` 1.8 m/s, a stagger under `KNOCK_MIN` 5.2, else knocked
down and up again), the player's `LAYER_PEOPLE`, `RAY_MASK`, `BUMP_STAGGER_KEEP`, `BUMP_KNOCK_KEEP`;
`climb_look.gd --program=bump`. Milestone 209 (decision 0351): their robes `FellowCloth3D` on the figure's skin
(`rooftops_crowd.gd`'s `_skirt()`: `SKIRT_TOP` 1.0 m, `SKIRT_HEM` 0.27 m, `SKIRT_FREE` 0.22 m from the skinned place,
`SKIRT_SIMULATE` 32 m), `climb_look.gd --program=people`. Milestone 210 (decision 0352): the hay cart's hay made at
run time (`rooftops_hay.gd` on the cart: `straws` 26,000, `ground_straws` 500, `seed`; `TOP` 2.02 m, `SPILL` 0.24 m;
`burst(at)`), `climb_look.gd --program=hay`. Milestone 211 (decision 0353): the weather on the keys 1-7 and 0 (`rooftops_weather.gd`: `transition` 10 s,
`cycle_hold` 25 s, `sky_dim` 0.85, `FAIR`, `FOG_NEAREST` 260 m, `LAMPS_DARK` 2.4; `--weather=<1-7>`,
`--weather_cycle=1`, `turn_to()`), a brazier and the chimneys' smoke (`rooftops_fire.gd`: `kind` "brazier" |
"chimney", `resolution`, `sleep` 70 m, `wind_scale` 0.2 | 0.6, `wind_max` 2.5 m/s; tools: `--gas_set_<kind>=`,
`--emit_set_<kind>=`), `climb_look.gd --program=weather` (`--view`, `--weather_seq`, `--weather_transition`),
`--perf=1`, `--setting=`, `--drop=`, `--draw_all=1`. Milestone 212 (decision 0354): the people's social layer
(`rooftops_crowd.gd`: `LOOK_NEAR` 7 m, `LOOK_UP_FAR` 24 m, `LOOK_MAX` 10, `TALK_REACH` 2.4 m, `POINT_EVERY` 6 s,
`RAIN_LEAVE` 0.3, `RAIN_BACK` 0.12, `COLD` 5 °C, `HURRY` 1.45, `fire_path`, `fire_people` 3, `weather_path`;
`talk_to_nearest()`, `get_social_stats()`), speech bubbles (`rooftops_bubbles.gd`: `say()`, `hint()`, `hush()`),
`climb_look.gd --program=talk`, `--program=lookup`, `--crowd=1`. Milestone 213 (decision 0355): CasaRosa's osteria
(`rooftops_osteria.gd` on the building: `get_people()`, `get_shelter()`, `get_stats()`), `climb_look.gd
--program=osteria` (`--view=door|inside|walkin`).

## Vehicles' marks, smoke and sounds (milestone 79)

Module `fellow_world`; nothing changes without the node (decision 0187). `FellowVehicleEffects3D` (a `Node3D`, a child
of a `FellowVehicle3D`; the FellowEngine menu's "Add Car" includes one). A tyre slides past `slip_min` (1.5 m/s over
the ground) and `slide_slip` (0.2 combined slip), fully at `slip_full` (6 m/s). Skid marks: `skid_marks` (on),
`max_mark_segments` (512), `mark_width` (0.22 m), `mark_step` (0.3 m), `mark_opacity` (0.8); `clear_marks()`. Smoke:
`smoke` (on), `smoke_amount` (40), `smoke_lifetime` (2.2 s), `smoke_color`. Sound: `sound` (on), `cylinders` (4),
`engine_volume_db` (0), `tyre_volume_db` (−4), `road_volume_db` (−10), `engine_offset` (0, 0.6, −1.3 m),
`sound_unit_size` (8), `sound_distance` (80 m); `get_audio_player()`. `get_stats()`, `get_global_stats()`/
`reset_global_stats()`. `FellowWheel3D.get_slip_speed()`, `get_contact_normal()`. Bench `bench/vehicle_fx_check.gd`
(`--listen=1` under Movie Maker, `--pictures=1`, `--many=N`).

Loose ground and fading marks (milestone 88, decision 0196), on `FellowVehicleEffects3D`: `loose_ground_group`
("fellow_loose_ground": colliders in it are loose ground; their metadata `fellow_dust_color` the dust's colour),
`dust` (on), `dust_amount` (48), `dust_lifetime` (1.6 s), `dust_color`, `dust_min_speed` (2 m/s), `dust_full_speed`
(15 m/s), `rut_opacity` (0.3); `mark_lifetime` (0 s: the marks stay); stats `dusting`, `on_loose_ground`,
`visible_segments`.

Dents (milestone 105, decision 0213): `FellowVehicleDamage3D` (a `Node3D`, a child of a `FellowVehicle3D`; it turns the
vehicle's contact reporting on): `impulse_threshold` (2 500 N·s in a physics step), `softness` (2·10⁻⁵ m per N·s
beyond it), `max_depth` (0.3 m), `radius` (0.7 m), `mesh_paths` (empty: every mesh of the vehicle but its wheels');
`apply_impact(position, direction, impulse)`, `repair()`, `get_damage()` (0-1), `get_deepest_dent()`,
`get_vertex_offset()`, `get_stats()`; the signal `impacted(position, impulse, depth)`. Bench `bench/vehicle_check.gd`
(the lanes `crash`, `tap`). The running gear (milestone 107, decision 0215; both 0 by default): `wheel_bend` (degrees:
the most a dent bends a wheel's toe, the way the push turns it), `engine_power_loss` (the share of `max_torque` a dent
of `max_depth` at `engine_position`, 0, 0.5, −1.4 m, takes); an impact reaches `radius` across its push, twice that
along it; `get_engine_power()`, `get_wheel_toe_change()`; `repair()` puts them back. `FellowWheel3D.toe` (degrees, +
in). Bench `bench/vehicle_check.gd` (the lanes `straight`, `bent`, `bent_off`, `engine`, `repaired`). Parts that come off
(milestone 108, decision 0216): `detachable_paths` (empty: none), `detach_depth` (0.12 m of dent at the part, added
up), `detach_mass` (8 kg: the body it falls away in, not colliding with the vehicle); `is_part_detached()`, the signal
`part_detached(part, body)`; `repair()` puts them back. Bench `bench/vehicle_check.gd` (the lane `bumper`). Hanging
(milestone 109, decision 0217): `hang_depth` (0: never; a part hangs on a pin at its end farthest from the hit until
`detach_depth`), `is_part_hanging()`, the signal `part_hanging(part, body)` (the lane `hang`). The FellowEngine menu's "Add Car"
(milestone 110) includes a bumper and a `FellowVehicleDamage3D` (`wheel_bend` 4°, `engine_power_loss` 0.4, the bumper
hanging at 6 cm and off at 16 cm) and subdivides the body for the dents; the regression scene `vehicle_damage`. Engine
smoke (milestone 110): `engine_smoke` (on; with `engine_power_loss` set, smoke from above `engine_position` as deep as
the dent there, from a tenth of `max_depth`), `is_engine_smoking()`. Suspension damage (milestone 110): `suspension_damage` (m,
0: off; the most a dent shortens a wheel's `suspension_length`), `get_wheel_sag()` (the lane `sagged`). Hinged parts
(milestone 110): a part's metadata `fellow_hinge_axis` (a `Vector3` in its space) makes it hang on a `HingeJoint3D`
along it instead of a pin, `fellow_hinge_limits` (a `Vector2` of degrees; a left door with its axis up opens outwards with
(0, 70)) (the lane `door`).

## Sound through the scene's geometry (milestone 78)

Module `fellow_world`; nothing changes without the node (decision 0186). `FellowAcoustics3D` (a `Node3D`; where it is
does not matter; one works at a time, the last to enter the tree) acts on every `AudioStreamPlayer3D` in the tree but
those in the group `fellow_no_acoustics`. `enabled`, `listener_path` (empty: the viewport's `AudioListener3D` or
camera), `collision_mask` (1). Occlusion: `occlusion_rays` (5), `source_radius` (0.5 m), `occluded_volume_db` (−12),
`occluded_shelf_db` (−18), `occluded_cutoff_hz` (1 000), `max_distance` (80 m), `max_rays_per_frame` (512),
`response_time` (0.12 s). Reverb: `reverb` (on), `reverb_bus` ("FellowReverb": made when missing, sending to Master),
`reverb_rays` (32), `reverb_interval` (0.25 s), `reverb_max_distance` (40 m), `absorption` (0.3), `reverb_level` (0.5),
`reverb_response` (0.5 s). `get_occlusion(player)`, `get_room()`, `get_stats()`, `get_global_stats()`/
`reset_global_stats()`; the monitor "FellowEngine/Acoustics time"; the FellowEngine menu's "Add Acoustics". The hook
`AudioStreamPlayer3D::fellow_set_acoustics()` (C++). Bench `bench/acoustics_check.gd` (`--listen=1` under Movie Maker,
`--many=N`). Milestone 90 (decision 0198): a collider's metadata `fellow_occlusion` (0 to 1; without it 1) is how much it
blocks a path (glass 0.3, a curtain 0.15); milestone 91 (decision 0199): up to three colliders on a path add up (at
most a full block); milestone 92 (decision 0200): `fellow_occlusion_highs` (0 to 1; without it as `fellow_occlusion`)
blocks the highs (the shelf and the cutoff) apart from the level; `get_occlusion_highs()`.

Openings (milestone 99, decision 0207): `FellowAcousticPortal3D` (a `Node3D`: `size` 1 × 2.1 m in its XY plane,
`transmission` 1; a gizmo with width and height handles; FellowEngine menu → Add Acoustic Portal). On
`FellowAcoustics3D`: `portals` (on), `portal_diffraction_db` (−6 dB a quarter turn), `max_portals` (2 tried a blocked
source, two rays each); `get_portal_weight(player)`, `get_heard_position(player)`; `get_stats()` adds `portal_paths`
and `portals`. A blocked source is heard through the nearest open portal between it and the listener when both legs
are clear and that way is louder: from the opening's direction at the path's length, its occlusion the bend's and the
transmission's. Bench `bench/acoustics_check.gd` (`--portals=1` with `--many`).
Round edges (milestone 148, decision 0277), on `FellowAcoustics3D`: `edge_diffraction` (off; "Add Acoustics" sets it on: a source still blocked
with no opening found is heard round the edge of what blocks it — its side or its top — when louder that way, from the
edge's direction at the path's length), `edge_reach` (4 m across the line, each way and up), `edge_diffraction_db`
(−9 dB a quarter turn); the path kept while its legs stay clear (searched again every fourth test; a failed search
waits four), up to about 45 rays a search from `max_rays_per_frame`; `get_portal_weight()` counts it, `get_stats()`
adds `edge_paths`. Bench `bench/acoustics_check.gd` (`--edges=1` with `--many`).

Each source's own room (milestone 111, decision 0218): `FellowAcoustics3D.source_rooms` (0: every source sent to the
listener's reverb; "Add Acoustics" sets 2) — that many reverbs more, on buses named `reverb_bus` + "Room1"...; one
audible source's room measured a frame by rays from it; a source in a room of its own (enclosed by at least half) unlike
the listener's (enclosure more than 0.2 apart or mean distance more than × 1.5) is sent to a room reverb (shared by
alike rooms, driven by their mean) by its own room's enclosure. `get_source_room(player)`; `get_stats()` adds
`room_buses`, `room_buses_used`, `room_measures`. Bench `bench/acoustics_check.gd` (`--rooms=N` with `--many`).

## Sound events, voices and mix snapshots (milestone 82)

Module `fellow_world`; nothing changes without the node (decision 0190). `FellowSoundEvent` (a resource): `streams`,
`selection` (`SELECTION_RANDOM_NO_REPEAT`, `SELECTION_RANDOM`, `SELECTION_SEQUENCE`), `volume_db` (0),
`volume_random_db` (0: ± uniform), `pitch_scale` (1), `pitch_random_semitones` (0), `max_instances` (0: no limit),
`cooldown` (0 s), `priority` (128, 0-255), `bus` ("Master"), `spatial` (on), `unit_size` (10), `max_distance` (0: no
limit), `attenuation_model` (inverse distance); `get_audibility_db(distance)`. `FellowAudioDirector` (a `Node`):
`max_voices` (32), `steal_voices` (on), `steal_fade` (0.03 s), `snapshots`, `seed` (0), `listener_path` (empty: the
viewport's `AudioListener3D` or camera); `play(event, position)`, `play_on(event, node)` (a voice's id, 0 when not
played), `stop(voice, fade)`, `stop_event()`, `stop_all()`, `is_playing()`, `get_voice_count()`,
`get_event_voice_count()`, `get_voice_player()`, `get_voices()`, `push_snapshot()`, `pop_snapshot()`,
`get_snapshot_weight()`, `get_stats()`/`reset_stats()`; signal `voice_finished(voice, stolen)`. `FellowMixSnapshot` (a
resource): `bus_volumes` (bus → dB added), `fade_in` (0.3 s), `fade_out` (0.5 s), `trigger_bus` (empty: pushed and
popped; set: active while a voice of the director plays on it). Bench `bench/audio_director_check.gd` (under Movie
Maker; `--many=1`). Milestone 85 (decision 0193), on `FellowSoundEvent`: `virtual_voice` (off: on, a sound with no
voice for it, giving its voice up or out of reach goes on unheard and is resumed where it would be), `steal_oldest`
(off: on, over `max_instances` the oldest gives way); on the director `is_virtual()`, the stats `virtual`,
`virtualized`, `realized` (`--many=1 --virtual=1`). Milestone 87 (decision 0195), on `FellowMixSnapshot`:
`bus_lowpass` (bus → cutoff Hz; the director's own low-pass at the end of the bus, faded in octaves with the
snapshot's weight, off when no snapshot filters the bus).
Milestone 96 (decision 0204), on `FellowAudioDirector`: `bus_voice_limits` (bus → voices at most on it; a sound over
it takes the voice of the bus's least important, quietest, or goes virtual or is refused).

## Vehicles (milestone 77)

Module `fellow_world`; nothing changes without the nodes (decision 0185). `FellowVehicle3D` (a `RigidBody3D`, 1 400 kg,
its linear and angular damping replaced by 0: the node's own air) and its `FellowWheel3D` children (each marking a
wheel's centre with the suspension fully extended; it moves along the suspension, steers and rolls). Input: `throttle`,
`brake` (0-1), `steering` (-1 to 1; positive steers left, as `VehicleBody3D`'s), `handbrake`. Engine: `max_torque` (400 N·m), `idle_rpm` (900),
`peak_rpm` (4 500), `max_rpm` (7 000, the limiter), `torque_curve` (a `Curve` over rpm / `max_rpm`; empty: a built-in
one), `engine_braking` (70 N·m at `max_rpm` with the throttle closed), `engine_inertia` (0.25 kg·m²). Drivetrain:
`gear_ratios` (3.6, 2.2, 1.5, 1.15, 0.92, 0.76), `reverse_ratio` (3.4), `max_reverse_speed` (8 m/s), `final_drive`
(3.7), `drivetrain_efficiency` (0.9), `automatic` (on: up at `shift_up_rpm` 6 300 of the road speed at full throttle,
earlier with less, down under `shift_down_rpm` 2 600, kickdown with the throttle floored, `shift_time` 0.25 s without
drive; reverse after 0.5 s stopped with the brake held and the handbrake off, the brake then driving it backwards), `differential` (open: the torque shared equally; locked: an
axle's driven wheels turn together), `traction_control` (on). Brakes and steering: `brake_torque` (2 500 N·m a wheel),
`handbrake_torque` (3 500), `anti_lock_brakes` (on), `max_steer` (35°), `steer_at_speed` (0.35 of it at 100 km/h),
`steer_speed` (4 rad/s). Body: `anti_roll` (8 000 N/m an axle), `drag_area` (0.7 m²), `downforce_area` (0.3 m²),
`substeps` (4). `FellowWheel3D`: `radius` (0.34 m), `suspension_length` (0.3 m), `spring_rate` (40 000 N/m),
`damping_compression` (3 000 N·s/m), `damping_rebound` (4 000), `grip` (1: the tyre's peak friction, times the ground's
physics material's friction), `rolling_resistance` (0.012 of the load), `inertia` (1.2 kg·m²), `steering`, `driven`, `braked` (on), `handbrake`;
`is_in_contact()`, `get_compression()`, `get_load()`, `get_rpm()`, `get_slip_ratio()`, `get_slip_angle()`,
`get_contact_point()`, `get_ground()`. The vehicle's `get_speed()`, `get_speed_kmh()`, `get_engine_rpm()`, `get_gear()`,
`set_gear()` (manual), `get_stats()`, `get_global_stats()`/`reset_global_stats()`; signal `gear_changed(gear)`. The
FellowEngine menu's "Add Car"; the wheel's gizmo (its circle and its suspension's travel). Bench `bench/vehicle_check.gd`
(`--many=N`, `--pictures=1`); the regression scene `vehicle`.

Milestone 147 (decision 0274), on `FellowVehicle3D`: `steering_mode` (`STEERING_WHEELS`; `STEERING_SKID`: no wheel
turns, a steering gear holds the inner side's speed at k times the outer's — k = 1 - |steering|·(2 - throttle): 1
straight, 0 about the inner side with the throttle held, -1 on the spot without it; at speed a radius of at least
v² / (0.7·μ·g·|steering|); a locked differential does not apply), `skid_turn_drive` (0.6: the throttle a full steering
gives on its own near a standstill, gone by 4 m/s), `tracks` (off: each side's wheels turning together, as a track links
them); `get_stats()`'s `skid` and `skid_ratio`. The FellowEngine menu's "Add Tracked Vehicle"; `vehicle_check.gd
--many=N --tank=1`.

Milestone 147 (decision 0275), on `FellowVehicle3D`: `balance` (off: a two-wheeler balanced by its rider — two wheels
in line; the steering asks for a lean, the least of `max_lean`, what 0.8 of the grip holds and what the front wheel's
lock turns at the speed, held by a torque about the body's forward axis, the front wheel turned to the turn the lean
balances; upright at a standstill), `max_lean` (45°), `is_fallen()` (past `max_lean` by 25°: the rider lets it fall);
`get_stats()`'s `lean_deg`, `lean_target_deg`, `fallen`. The FellowEngine menu's "Add Motorcycle"; `vehicle_check.gd
--many=N --bike=1`.

Contacts (milestone 101, decision 0209): on `FellowWheel3D`, `contact_shape` (`CONTACT_RAY`, the default;
`CONTACT_SPHERE`; `CONTACT_CYLINDER` of `radius` and `width`, 0.22 m, along the steered axle): the tyre's shape swept down
the suspension — curbs met by the tyre's front, gaps narrower than it bridged; two queries a wheel a step on an edge,
one on flat ground (for the player's car; rays for traffic). Bench `bench/vehicle_check.gd` (`--contact=1|2` with
`--many`).

## Destruction (milestone 74)

Module `fellow_world`; nothing changes without the node (decision 0182). `FellowDestructible3D` (a `Node3D`), a child of
the body to break (`body_path`: by default the parent if a `CollisionObject3D`; its layers and mask go to the pieces),
breaking its `MeshInstance3D` (`mesh_path`: by default the parent's first one). Fracture: `piece_count` (12),
`seed` (0), `impact_focus` (0.5: the share of the sites around the blow), `interior_material` (the mesh's first surface's
when empty), `interior_uv_scale` (1: UVs in metres), `prefracture` (off: computed on a worker when it breaks; on:
computed at ready on the main thread and shared by the same mesh, seed and count). Breaking: `health` (100,
`damage()`), `break_on_impact` (on), `break_impulse` (40 N·s), `free_after_break` (on). Pieces: `density` (600 kg/m³),
`burst_speed` (1.5 m/s), `debris_lifetime` (10 s asleep; 0: until the budget). For the whole game:
`set_max_pieces_per_frame()` (128; 0: no limit), `set_max_debris()` (256), `get_debris_count()`, `clear_cache()`,
`get_global_stats()`/`reset_global_stats()`; the monitor "FellowEngine/Debris pieces". `break_apart()`, `damage()`,
`is_broken()`, `get_current_health()`, `get_stats()`; signals `broken(pieces)`, `damaged(amount, health)`. The
FellowEngine menu's "Add Destructible Crate". Milestone 80 (decision 0188): `structural` (off; a static object stands
in its pieces, broken where struck), `break_radius` (0.6 m), `anchor_height` (0.1 m); `get_standing_pieces()`,
`get_structure()`; "Add Destructible Wall"; bench `bench/structure_check.gd`, the regression scene `structure`. Bench `bench/destruction_check.gd` (`--many=N`, `--prefracture=1`); the
regression scene `destruction`. Milestone 122 (decision 0246): `strength` (0 kPa: support by connectivity alone; with
a strength, the faces where pieces touch are elastic bonds carrying the structure's weight — at each blow and
`update_structure()` the stresses are solved and a bond past the strength in tension or shear, or ten times it in
compression, cracks, the worst first; what no longer reaches the ground falls), `anchor_mode` (Lowest Point: the pieces
within `anchor_height` of the object's lowest point; Static Bodies: the pieces touching another static body — a
balcony on a wall); `update_structure()` (a prefractured structural object stood in its pieces and settled without a
blow), `add_load()`/`clear_loads()` (what rests on it), `get_bonds()` (each bond's face, force, moment, stresses and ratio); `get_stats()` adds `last_cracked`,
`stress_usec`, `stress_iterations`, `bonds`. Bench `bench/stress_check.gd`, the regression scene `stress`. Milestone 123
(decision 0247): `landing_break_speed` (0 m/s: the parts land whole; a part of several pieces the structure drops
breaks where it lands when its speed changes by more than this in a step — the pieces near the contact each a body,
the rest split along the bonds that hold); `get_stats()` adds `falling_parts`, `last_landing_breaks`,
`landing_breaks_total`. Bench `bench/landing_check.gd`.

## Cloth (milestone 73)

Module `fellow_fluid`; nothing changes without the node (decision 0181). `FellowCloth3D` (a `GeometryInstance3D`): a
rectangle (`size` 2 × 1.2 m from the node's origin along +X and down, `resolution` 24 cells along its longer side) or a
`source_mesh`'s first surface, pinned by `pin_mode` (top edge, left edge, top corners, corners, or vertex colours: red
0.5 or more), simulated on the CPU by position-based dynamics: `mass` (0.25 kg/m²), `stretch_stiffness` (1),
`bend_stiffness` (0.2), `damping` (0.05 a second), `tether_slack` (1.08; 0: no tethers), `gravity_scale`,
`substep_rate` (480 Hz), `iterations` (2 stretch passes a substep), `max_substeps` (12 a frame). The air: the active
`FellowWind3D`'s wind at the cloth times `wind_scale`, plus `wind`, `drag` (1), `lift` (0.3), `turbulence` (0.3).
Collisions: `thickness` (2 cm), `friction` (0.3), `colliders` (`CollisionShape3D`s or `CollisionObject3D`s, followed as
they move), `collide_with_world` (on: the physics world's bodies under the cloth on `collision_mask`; spheres, capsules,
boxes, cylinders, planes, height maps). `lod_distance` (25 m: half the substeps past it), `simulation_distance` (100 m:
carried, not simulated, past it), `simulating`, `material` (a two-sided default). `reset()`, `apply_impulse()`,
`get_particle_count()`, `get_particle_position()`, `set_particle_pinned()`, `is_particle_pinned()`, `get_stats()`,
`get_global_stats()`/`reset_global_stats()`; the monitor "FellowEngine/Cloth time". Every cloth of a frame on the worker
threads at once; drawn with motion vectors (one blend shape: per-instance vertex buffers swapped each frame). The
FellowEngine menu's "Add Flag" and "Add Curtain". Bench `bench/cloth_check.gd` (`--many=N`); the regression scene
`cloth`.

Milestone 76 (decision 0184): `skeleton_path` (a `Skeleton3D`: the pinned particles follow its bones, each bound to
the nearest of `pin_bones` — empty: any — when the cloth is built or reset, interpolated over the substeps); capsules
on the bones (`BoneAttachment3D`s with `CollisionShape3D`s) in `colliders` for the body. `get_stats()` counts
`bone_pins`. Milestone 116 (decision 0230): `follow_skin` (off) with a `source_mesh` that has bone weights (a skinned
mesh's surface, in the skeleton's space): every particle skinned with the skeleton as Godot skins the mesh (`skin`: the
binds; none: the skeleton's rest); the pinned ones follow their skinned places (blended between bones), the free ones
are kept within `skin_max_distance` (m; 0: anywhere) of theirs; past `simulation_distance` the cloth is skinned.
`get_stats()`' `skin_particles`, `skin_limited`. Milestone 117 (decision 0232): `skin_distance_mode` — each particle's
share of `skin_max_distance`: `SKIN_DISTANCE_UNIFORM` (the default, all of it), `SKIN_DISTANCE_FROM_PINS` (growing
along the cloth from the pins: none at a pin, all of it at the farthest particle — a skirt's waist on the hips and its
hem free, nothing to paint), `SKIN_DISTANCE_VERTEX_COLOR` (painted in the source mesh's vertex colours' green).
Milestone 147 (decision 0273): `skin_backstop` (off) — no free particle more than `skin_backstop_distance` (m, 0)
behind its skinned place along its skinned normal (into the body): a garment kept on the body without colliders where
its rest shape follows it; `get_stats()`' `skin_backstopped`, `skin_behind`.

Milestone 112 (decision 0220): tearing. `tear(position, radius)` breaks every edge passing within the radius (a shot, a
cut: a line of calls); `tear_strain` (0: off) breaks an edge stretched past that fraction of its length (0.5: half
again as long; checked once a frame). A broken edge's triangles go (a hole, not drawn, no wind on them), with the edges
left without a triangle and the bending pairs across one; the tethers are found again along what is left (a part cut
from every pin falls free); a part fallen 100 m below the cloth is dropped. The signal `torn(position, edges)`,
`get_torn_edge_count()`, `get_stats()`' `torn_edges`, `triangles_all`, `loose_particles`; `reset()` makes the cloth
whole again. Self-collision (decision 0221): `self_collision` (off) keeps every two particles at least
`self_collision_distance` apart (0: three quarters of the mean edge), but for those as close at rest; the pairs found
once a frame by a spatial hash, pushed apart in each substep. `get_stats()`' `self_pairs`, `self_contacts`,
`self_distance`. Milestone 115: `self_collision_triangles` (off; decision 0228) keeps each particle at the distance from
the other layers' triangles too, on the side it was found on (the distance then defaults to a quarter of the mean edge:
layers that close without a particle slipping between another layer's); `self_collision_friction` (0-1, 0; decision
0229) takes the layers' sliding along each other away where they touch (a heap on a slope held by its own layers).
`get_stats()`' `self_tri_pairs`, `self_tri_contacts`. Milestone 118: `self_collision_edges` (off; decision 0235) keeps the
edges at the distance from the other layers' edges too (two edges crossing between particles: a towel across a bar of the
same cloth); the pairs come from the particles nearer each other than the reach and the longest edge and not near along
the cloth at rest, so a cloth that does not come near itself costs next to nothing. `get_stats()`' `self_edge_pairs`,
`self_edge_contacts`.

## Hair (milestone 124)

Module `fellow_fluid`; nothing changes without the node (decision 0248). `FellowHair3D` (a `GeometryInstance3D`), under
a `BoneAttachment3D` on the head bone: the scalp is a sphere's cap (`scalp_radius`, `scalp_center`, `coverage` from its
top, `forehead_angle` about the face's -Z left bare). `guide_count` guides (160) of `segments` (12) and `length`
(0.32 m) combed along the scalp and falling, simulated on the CPU each frame (`substeps` 4: roots on the scalp, lengths
exact, `stiffness` back to the styled shape fading to the tip, `damping`, `gravity_scale`, the scene's `FellowWind3D`
by `wind_scale` and `drag`, out of the scalp's sphere by `collision_margin` and out of `colliders` — sphere and capsule
`CollisionShape3D`s); `simulating` off keeps the styled shape. `strand_count` strands (10 000) drawn between their three
nearest guides (`spread`; `length_variation`, `curl`, `curl_frequency`, `seed`) as camera-facing ribbons at least a pixel
wide (`root_width` 0.4 mm, `tip_width` 0.12 mm; the coverage of thinner ones dithered: TAA or FSR), lit by a hair model
(`root_color`, `tip_color`, `color_variation`, `specular_shift`, `specular_strength`, `specular_sharpness`,
`transmission`, `root_occlusion`); motion vectors follow the strands. `reset()`, `get_stats()`, `get_guide_points()`,
`get_guide_rest_points()`. "Add Hair" in the FellowEngine menu. Bench `bench/hair_check.gd`, the regression scene `hair`.
Milestone 125 (decision 0249): `scalp_mesh` (roots on the faces whose three vertices' red is 0.5 or more; the sphere
stays the collider), `lod_distance` (4 m: beyond it a share of the strands by the square of the distance through a
75-degree view, at least `min_strand_share`, wider in proportion), `simulation_distance` (40 m); `get_stats()` adds
`simulated`, `visible_strands`, `width_scale`. Bench `bench/hair_lod_check.gd`. Milestone 126 (decision 0250): every
hair node of a frame stepped on the worker threads at once; `FellowHair3D.get_global_stats()` (`group_usec`,
`simulate_usec`, ...), `reset_global_stats()`. Bench `bench/hair_crowd_check.gd`. Milestone 144 (decision 0268):
`self_shadow` (0: off, the material as before; 0-1, on in the "Add Hair" preset) — the hair shadows itself from a
directional light (`self_shadow_light`, or the scene's first visible `DirectionalLight3D`) through a deep opacity map of
its guides' points made on the worker threads each frame (about 75 µs a head on one core), `self_shadow_density` (0.5:
how opaque the strands are to light crossing them); `get_stats()` adds `self_shadow_usec`, `self_shadow_lit`,
`self_shadow_min`, `self_shadow_mean`, `get_global_stats()` `self_shadow_usec`. `hair_check.gd --self_shadow=1`,
`hair_crowd_check.gd --self_shadow=1`. Milestone 149 (decision 0280): `self_shadow_light` may be an `OmniLight3D` or a
`SpotLight3D` (the map along the direction towards it from the hair's centre, the material dimming the light that is
not directional within about 18° of it); `hair_check.gd --lamp=<m>`.

## Characters up and down steps (milestone 169)

`CharacterBody3D.floor_step_height` (m, 0 by default: stock's behaviour bit for bit; decision 0311). Above 0, while the
body is on the floor and not moving up, `move_and_slide()`:

* steps up a riser or a step's edge it is stopped by, if the step's top is a floor no higher than this above the floor
  it stands on and there is room above (cast up, along the motion left, down; at once — `get_last_step()` says how far,
  for a camera or model to ease it out);
* snaps down a step or ledge no higher than this as well as `floor_snap_length` (no fall down each step);
* counts a steep contact under its rounded bottom on a step's edge as floor (a walkable top just past it, a floor no
  more than a step below on the body's side): it stands on the edge or rolls over it.

Typical: 0.3-0.4 m for a person-sized capsule; the editor's mantling character preset sets 0.35 m. The character
course (`benchmarks/tools/character_course/run_course.py`) measures it on both physics engines.

## Characters push rigid bodies and weigh on them (milestone 170)

`CharacterBody3D.rigid_push_force` (N) and `rigid_standing_mass` (kg), 0 by default (stock's behaviour bit for bit;
decision 0312). With a push force, after `move_and_slide()` each rigid body a slide was stopped by gets the impulse
that brings it to the velocity the body asked for — along the ground, at its centre of mass's height (crates slide
rather than tip), its mass and inertia at that point taken into account — at most the force for the tick: light crates
move at the character's pace, heavy ones slowly or not at all against their friction; a step up never lands on a rigid
body then, it is pushed. With a standing mass, the rigid body stood on gets that weight at the floor's contact (seesaws,
boats, planks). Typical: 300-600 N, the character's mass. `run_course.py --course push` measures it.

## Inertialized animation transitions (milestone 171)

`FellowInertializer3D` (module `fellow_animation`; decision 0313), a `SkeletonModifier3D` under the `Skeleton3D`: let
the animation switch at once (`AnimationPlayer.play()` with no blend, an `AnimationTree`'s transitions with no
cross-fade) and the node blends over `blend_time` (0.25 s) from the last pose drawn, carried on by its velocity
(halving every `extrapolation_halflife`, 0.05 s), to the new animation's. Blends start on `blend(seconds)`, on the
`animation_started` of the mixer at `animation_mixer_path`, or with `detect_jumps` (off) when a bone turns more than
`jump_angle` (20°) further in a frame than its velocity carries it. `is_blending()`, `get_blend_progress()`,
`get_stats()`. Against a cross-fade of the same length (CMU clips): the bones' acceleration's median 41-46 % lower,
10-11 % less foot sliding, one animation evaluated instead of two (corrected in milestone 172).

Feet held through transitions (milestone 172, decision 0314): a `FellowFootLock3D` after the inertializer (both under
the `Skeleton3D`, the lock second) holds the feet the new animation plants while the old pose fades: 26 % less sliding
in transitions than a cross-fade, the held legs' acceleration's 95th percentile 4.2 times the inertializer's alone.
`FellowFootLock3D.release_extension` (1.0: off) lets a held foot go when holding it needs more than this share of the
leg's length from the hip (0.98: the legs' acceleration 1.7 times, the feet held half as long). On a rig whose feet
rest more than 0.1 m below the skeleton's origin (mocap resting on its hips) the lock takes the feet's lowest joint at
rest as the ground. The course: `benchmarks/tools/animation_transitions/transitions.gd` (`--contact`, `--unlock`,
`--lock_halflife`, `--release`).

## Foot placement (milestone 174)

`FellowFootPlacement3D` (module `fellow_animation`; decision 0316), a `SkeletonModifier3D` under the `Skeleton3D`, after
the animation and after a `FellowFootLock3D`: the feet of clips made on flat ground put on the ground under them.

| property | default | meaning |
|---|---|---|
| `left_foot_bone`, `right_foot_bone` | `LeftFoot`, `RightFoot` | the feet (their parents and grandparents: the legs) |
| `left_toe_bone`, `right_toe_bone` | `LeftToes`, `RightToes` | the toes (their joints: the balls' points; empty: the heel only) |
| `collision_mask` | 1 | the layers the feet stand on (the character's own bodies are left out) |
| `probe_up`, `probe_down` | 0.5, 0.6 m | the rays from this far over each foot to this far under the character's ground: as far as a foot is raised or lowered |
| `heel_offset` | 0.05 m | the heel's point behind the ankle |
| `contact_height`, `swing_height` | 0.05, 0.2 m | a sole within the first of the animation's ground is down (turned to the ground), the second above that in the air |
| `foot_alignment`, `max_alignment_angle` | 1.0, 40° | how far a foot down is turned to the ground |
| `max_hips_drop` | 0.45 m | the furthest the hips move from the animation's |
| `raise_halflife`, `lower_halflife`, `hips_halflife` | 0.04, 0.1, 0.08 s | the easing: up onto higher ground (and down while in the air), down while a foot is down (and the turn), the hips |

Methods: `get_foot_offset(foot)`, `get_hips_drop()`, `get_stats()` (`frames`, `rays`, `usec_per_frame`), `reset()`
(after placing the character elsewhere). Give it its weight with `influence` (on the ground; not climbing, hanging or
in the air). Measured with `benchmarks/tools/foot_placement/placement.gd` (`--gait=walk|run`, `--lanes=`,
`--variants=off,on,lock,lock_on`, `--set=property:value,...`); pictured by `showcase/tools/foot_placement_lab.gd`.

## Joint limits (milestone 202)

`FellowJointLimits3D` (module `fellow_animation`; decision 0344), a `SkeletonModifier3D` under a humanoid `Skeleton3D`,
last among its modifiers (after the IK, the foot placement and a look-at): the pose kept within an adult's active range
of motion. Nothing changes without it.

| property | default | meaning |
|---|---|---|
| `hinge_alignment` | on | an elbow or a knee bent in its hinge's plane: the upper arm or the thigh turned about its own line (the elbow, the knee and the lower limb stay) until the hinge lies in the limb's bend; a limb under 8° of bend (fully from 20°) keeps the turn it had |
| `hinge_max_turn` | 120° | a limb bent backwards, further round than this, is left |
| `limit_ankles`, `ankle_range` | on, (−30°, 55°) | the foot's dorsi- and plantarflexion, turned about the ankle in the plane of the shin and the foot |
| `limit_wrists`, `wrist_flexion_range`, `wrist_deviation_range` | on, (−85°, 75°), (−40°, 25°) | the hand brought back along the way it is bent to an ellipse through the four limits (flexion −, extension +; ulnar −, radial +) |
| `wrist_pivot` | on | the hand turned about its middle knuckle, the arm brought to the wrist's new place (a hand on a hold stays on it); off: about the wrist |
| `limit_neck`, `neck_flexion_range`, `neck_bend_limit`, `neck_turn_limit` | on, (−70°, 60°), 45°, 85° | the head against the chest: flexion forwards (+) and back, the bend to the side, the turn |
| `max_correction_speed` | 720 °/s | each correction eased on and off (a hinge's at a quarter of this near straight, the full speed from 48° of bend; the wrists' at once); 0: at once |
| `bone_names` | {} | role → bone name, over the names found (Godot's humanoid profile, Mixamo's, `.L`/`.R`, `_l`/`_r`) |

Methods: `get_bones()` (the roles found), `get_stats()` (`frames`; how many corrections: `hinges`, `ankles`, `wrists`,
`necks`; the largest of each in degrees: `hinge_max`, `ankle_max`, `wrist_max`, `neck_max`; `usec` in all, `last_usec`).
The rest pose is taken as a T or an A, the thumbs and toes forwards; the wrists need the index, middle and little
fingers' first bones. Measured with `showcase/tools/anim_quality.py` (its anatomy table and gates, from
`showcase/tools/anatomy.gd`); the showcase's player: `--joint_limits=0`, `--hinge_alignment=0`, `--limit_ankles=0`,
`--limit_wrists=0`, `--limit_neck=0`, `--max_correction_speed=<°/s>`.

## Traversal: mantling and vaulting (milestone 72)

Module `fellow_animation`; nothing changes without the nodes (decision 0180). `FellowTraversal3D`, a child of the
character's body (`body_path`: by default the parent; its capsule from its first `CollisionShape3D`): each physics frame
while the body moves (`auto_mantle` on, `auto_min_speed` 0.5 m/s, or the controller's `set_intent()`) it looks for a
ledge in the way with the physics server's queries — a wall within `reach_distance` (0.6 m) turned no more than
`max_wall_angle` (45°), a top no steeper than `max_ledge_slope` (40°) between `min_height` (0.5 m) and `max_height`
(2.3 m; `grab_in_air` on: 0.5 m more in the air), room for the capsule on it, above the body and over the edge — and
moves the body onto it: `KIND_LOW` up to `low_max_height` (1.3 m, `low_duration` 0.6 s for 1 m), `KIND_HIGH` above
(`high_duration` 1.1 s for 1.8 m), or over a thin obstacle (`vault` on, `vault_max_height` 1.3 m, `vault_max_depth`
0.6 m; with no room on it or at `vault_min_speed` 2.5 m/s, `vault_duration` 0.6 s); `align_to_wall` (on),
`exit_speed` (1.5 m/s), `collision_mask`, `check_interval` (2: while nothing is in reach, a check every second
physics frame, staggered), `enabled`, `debug_draw`. `find_ledge()`, `try_mantle()`, `cancel()`,
`is_active()`, `get_action()`, `get_progress()`, `get_last_check()`, `get_pose_targets()`; signals `mantle_started`,
`mantle_finished`, `mantle_cancelled`; `get_global_stats()` / `reset_global_stats()` (checks, mantles, poses, their
time) and the monitor "FellowEngine/Traversal time". Clips: `low_clip`, `high_clip`, `vault_clip` (the mantle in the
clip's time), `clip_root_bone` (the clip's root motion warped onto the ledge as the body's path).

`FellowTraversalPose3D`, a modifier of the character's `Skeleton3D`: while a mantle runs, the torso leaned towards the
wall (`lean`), the hips lowered for the hands to reach the edge, the hands on it (`hand_ik`) and the feet on the wall's
face and the top (`foot_ik`) by two-bone IK over the animation; or the traversal's clip played. `traversal_path`
(found among the children of the skeleton's ancestors), `blend_time` (0.15 s), `bone_names` (roles to bones, over the
names found: Godot's humanoid profile, Mixamo, Unreal's mannequin, Blender's); `get_role_bone()`, `get_hips_drop()`,
`get_weight()`. The FellowEngine menu's "Add Mantling Character" (a body, its capsule, a traversal). Bench
`bench/traversal_check.gd` (the course; `--crowd=N`); the regression scene `traversal`.

Hanging and ladders (milestone 75, decision 0183), on `FellowTraversal3D`: `hang` (on: in the air, a ledge with no
room on its top is hung from when its edge is within 0.8 m below to 0.4 m above `hang_reach` over the feet),
`hang_first` (off: with it, ledges it could climb too), `hang_reach` (2.05 m), `shimmy_speed` (1 m/s); `ladders` (on),
`ladder_speed` (1.2 m/s), `ladder_reach` (0.6 m past the capsule). `try_hang()`, `try_ladder()`, `climb_up()`,
`release()`, `is_hanging()`, `is_on_ladder()`, `get_ladder()`, `set_climb_input()`; signals `hang_started`,
`hang_finished`, `ladder_started`, `ladder_finished`; actions `KIND_HANG`, `KIND_LADDER`. `FellowLadder3D` (a marker:
its foot at the node, climbed from +Z): `height` (3 m), `width` (0.5 m), `rung_spacing` (0.3 m), `rung_offset`
(0.25 m), `exit_at_top` (on), `enabled`. The FellowEngine menu's "Add Ladder". Bench `bench/climb_check.gd`; the
regression scene `climb`.

Corners and jumps (milestone 81, decision 0189), on `FellowTraversal3D`: `wrap_corners` (on: hanging, where the ledge
ends while the intent goes on along it, round an outside corner onto the wall's side or an inside one onto a wall
meeting it — the new edge at the same height, both hands on it, room for the body; the intent held as it was goes on
along the new edge), `hang_jump_distance` (1.6 m: how far past the edge's end `hang_jump()` looks along it),
`hang_jump_height` (1.3 m: how far over the edge it looks up). `hang_jump()` (to the next ledge the way the intent
goes, along or up; nothing when none), `is_transferring()`; signal `hang_transferred(ledge, how)`; `TRANSFER_*`
constants; the global stat `transfers`. Bench `bench/climb_check.gd` (ten lanes); the regression scene
`ledge_corners`. Over an edge into a hang (milestone 84, decision 0192): `drop_to_hang` (off: on, walking over the
edge of the floor — a `CharacterBody3D` on the floor, the intent towards it — hangs from it), `drop_max_speed` (2 m/s:
faster, it goes over); `try_drop(direction)`; `TRANSFER_DROP`. Bench `bench/climb_check.gd` (twelve lanes). Down
to a ledge below (milestone 86, decision 0194): `hang_jump()` with the wish down, `hang_jump_depth` (2.6 m);
`TRANSFER_JUMP_DOWN`. Diagonally (milestone 93, decision 0201): `hang_jump()` with the wish along and up or down;
`TRANSFER_JUMP_DIAGONAL`. Back to the wall behind (milestone 95, decision 0203): `hang_jump()` with the wish away from
the wall; `TRANSFER_JUMP_BACK`. Round a corner (milestone 149, decision 0279): `hang_jump()` with the wish along and no
edge along it — a ledge on the side of the wall where it turns away, 0.3 m to `hang_jump_distance` back from the
corner; `TRANSFER_JUMP_CORNER`.
Free climbing (milestone 148, decision 0276), on `FellowTraversal3D`: `free_climb` (off: on, a body pushing at a face
on a layer of `climb_mask` with nothing to mantle onto, or caught against it in the air, climbs it — up and down,
along it, round towers and corners, onto inside corners, over the top as a mantle or a hang, down to the floor),
`climb_mask` (1: the layers whose faces are climbable), `climb_speed` (1 m/s), `climb_max_overhang` (20°: a face
leaning over the body more is not climbed; one less steep than `max_ledge_slope` is walked on), `climb_jump_distance`
(1.2 m: `hang_jump()` while climbing lunges over the face the way the wish goes, as far as the face and room go).
`try_climb(direction)`, `is_climbing()`; `set_climb_input()` and the intent steer it, `release()` lets go; signals
`climb_started`, `climb_finished(at_top)`, `climb_jumped(distance)`; `KIND_CLIMB`; the global stat `climbs`. Bench
`bench/climb_check.gd` (ten lanes more).
From hold to hold (milestone 151, decision 0282), on `FellowTraversal3D`: `hang_reach_up` (0 m: hanging, a ledge up to
this far over the edge is climbed to hand over hand, 0.75 s, pushing up or at the wall; one further is leapt to),
`hang_brace_height` (0 m: the feet braced on the wall this high over the hanging body's feet); signal
`hang_transfer_started(ledge, how, duration, lead_hand)`; `TRANSFER_REACH_UP`. Across (milestone 152, decision 0285):
`hang_reach_side` (0 m: a gap to a ledge to the side up to this wide is crossed hand over hand, 0.8 s, holding the wish
along at the edge's end; with `hang_reach_up`, a ledge up and to the side too, pushing up and along);
`TRANSFER_REACH_SIDE`. Narrow holds (milestone 152, decision 0286): `hold_min_width` (0 m: a hold narrower than both
hands' 0.48 m, down to this wide — a beam's end, a brick — is held from its middle, each hand 5 cm in from an end;
`spread` in the ledge's dictionary). Over an eave (milestone 152, decision 0287): with `hang_reach_up`, a hold under an
eave is held where the hands have room and the eave's lip is reached up to, then mantled over onto the roof; a sloping
top (to `max_ledge_slope`) is climbed onto in any mode. Holds beside and out (milestone 152, decision 0288): with
`hang_reach_up`, a hold up and 0.3-0.9 m to the side is reached across, an eave's lip 0.3 m or more out is reached up to,
holds close over each other are found, and a hold needs 0.12 m free over its edge. Over a parapet (milestone 152,
decision 0289): with `hang_reach_up`, from a hang over a top 0.6 m thick at most onto a floor 0.3-1.6 m lower behind it.
The body's weight (milestone 153, decision 0290): on `FellowTraversal3D`, `hang_leap_windup` (0 s: a leap from a hold
winds up this long — drawn in to the wall, the feet coiled — then springs from rest and is caught with its speed, the
hands thrown to the ledge ahead of the body and held, the feet pushing off and braced again; the move longer by as much),
`hang_clip` (the clip of a mantle from a hang, in place of the high or low one; the pose's hint `body_offset`); on
`FellowTraversalPose3D`, `body_sway` (0: the hips on a damped spring behind the body, keeping this share of its speed when
it changes, never lower than the arms reach), `sway_frequency` (1.6 Hz), `sway_damping` (0.35 of critical), `sway_max`
(0.15 m), `limb_smoothing` (0 s: the hands' and feet's targets eased with this time constant); `get_sway()`. The hands
on the stone (decision 0291): `hang_fit` (off: hanging, the body moved up to 0.75 m until the shoulders are within reach
of the holds the hands grip — `hand_grips` in `get_pose_targets()`), `get_hang_fit_offset()`. Drawn between the ticks
(milestone 154, decision 0292): with physics interpolation on, the pose's targets are placed relative to the skeleton's
interpolated transform. The eyes on the hold (decision 0293): `look` and `look_weight` in `get_pose_targets()` (the
next hold through a move, along the ledge or up the wall the way the controller wishes while hanging, over the top
through a mantle) — for a `LookAtModifier3D` on the head. Legs that bear weight (milestone 155, decision 0295):
`FellowTraversalPose3D.soft_ik` (0: off; past this share of a limb's reach, 0.8-0.999, its target is eased towards the
reach — no limb snaps straight); `FellowFootLock3D` (a `SkeletonModifier3D`; nothing changes without it): each foot held
where its sole comes down (under the heel or the ball, within `contact_height`, 0.03 m, of the skeleton's origin's
height), only its slide taken out, by two-bone IK; let go when lifted or pulled `unlock_radius` (0.3 m) away, back to the
animation over `blend_halflife` (0.06 s); `heel_offset` (0.05 m), `left_foot_bone`, `right_foot_bone` (`LeftFoot`,
`RightFoot`; their parents are the legs), `left_toe_bone`, `right_toe_bone` (`LeftToes`, `RightToes`); `is_locked()`,
`get_slide()`, `reset()`. No pops (milestone 156, decision 0296): on `FellowTraversalPose3D`, `clip_blend` (0 s: a clip
the traversal plays — a mantle from a hang — fades in from the pose the limbs' IK gave the frame before over this
time), `pole_follow` (0: a limb's bend follows the clip's own by this weight against the pole's, which turns over where
a target passes its line), `max_joint_speed` (0 °/s: off; while the node poses the body, no bone turns faster than this
from its last pose); `FellowTraversal3D`, a mantle with a clip's root motion: held at the wall until the clip's root is
over the top, then let go over 18 % of the mantle (milestone 195, decision 0337: until the feet are within a fifth of
the rise of the top, at most 0.3 m, the hold eased in and let go at no more than 2 m/s over the clip's own speed; a
braced foot as far out as the stone standing out above it, its ankle 0.15 m off the stone, also through moves between
holds); motion-captured climbing (milestone 196, decision 0338), on `FellowTraversalPose3D`: `hand_flight_weight` (1:
the IK's arc throughout; below 1, a hand off its hold between holds is reached for by that share of the IK, the clip's
arm carrying it, the grip eased in and out over 0.1 s), `hand_slide` (0: off; hanging from one ledge, each hand's
target slid along the edge to the animation's hand — a shimmy's hands step as its clip's), and always, hanging and
between holds, the knees and the balls of the feet kept off the stone the feet brace on (the leg turned about its hip-
to-ankle line, the foot pitched about its ankle; `FellowTraversal3D` gives the stone in its pose hints); milestone 198
(decision 0340), on `FellowTraversalPose3D`: `ledge_grip` (0: off; hanging, a hand on a ledge held as a climber's — the
knuckles over the edge, the palm down the face, the fingers and thumb on the top, over a moulding or by a step's edge,
straightened towards the shoulder where the arm must reach, placed so all the way between holds; with it the hands'
targets eased over at most 15 ms, the hands in twice as fast as the pose, the elbows' bend at most 720°/s; needs the
hands' finger bones) and `hand_flight_weight` homing in over the second half of a hand's way; on `FellowTraversal3D`:
`set_leap_timing(kind, time, reach, progress, lift, hands)` (`LEAP_UP`, `LEAP_ALONG`; none by default: a leap from hold
to hold as a clip's — its hips' way and lift, its hands' let-go and catch, slower for a longer leap within 0.8-1.6
times), `clear_leap_timing()`, `has_leap_timing()`; milestone 200 (decision 0342), on `FellowTraversal3D`: `leap_gravity`
(0 m/s²: off; a timed leap up flies under it — a dip, a push from rest, the flight slowing by gravity alone, the hands
up ahead of the body and catching at the dead point, the settle into the hang), `get_leap_phases()` (where the clip's
push, flight and settle are played); with `ledge_grip`, a hand on its hold keeps its fingers over the edge and
`hang_fit` follows its goal's steady drift at once (up to 1.2 m/s); milestone 201 (decision 0343), on
`FellowTraversal3D`: `aim_jump_at_hold(direction, gravity, max_speed = 0)` (the controller's call at a jump: the takeoff
speed that brings the body to its top 8 cm over its hang from the hold in `direction` — the wall up to 1 m further than
`reach_distance` —, 0 where there is none or it needs more than `max_speed`; the hands reach for it as the body rises,
caught at the top with them on), `is_aiming()`; on `FellowTraversalPose3D`, while the hands reach from the air: no sway,
the grip's shape in at once, the clip's feet kept 10 cm off the stone (the knees 8 cm) until 0.4 s after the catch;
milestone 203 (decision 0345), on `FellowTraversal3D`: `leap_push_force` (0: no limit; a leap's push as a person's — a
half sine of support force from rest to that many body weights over at most 0.65 m, no dip on straight arms; a hold
higher than such a leap carries the body is not leapt to), `climb_pull_speed` (0 m/s: the reaches' fixed times; the body
pulled up between holds no faster, the leading hand reaching the hold as the body gets there), `hang_step_up_height`
(0 m: off; a hold beyond a leap and up to this far above stood up to from the ledge the hands are on, where its top is
0.12 m deep and a body fits on it: pulled up, pressed to straight arms with a foot on the top at 0.8 m/s at most, stood
up at 0.9 m/s, reached, hung — `TRANSFER_STEP_UP`), `get_aim_state()` (an aimed jump's `time`, `catch_time`,
`rise_time`, `hang_feet`, `inward`: the controller steers the body in the air to its hang); on `FellowTraversalPose3D`:
`sway_vertical` (1: the vertical share of `body_sway`'s drive — 0 sways the hanging body along the wall and out from it
only), `fit_max_speed` (0 m/s: `hang_fit` as fast as it eases; else no faster, its speed changing by at most 8 m/s²);
the showcase's player: `leap_push_force` 2.3, `climb_pull_speed` 1, `hang_step_up_height` 2.3, `sway_vertical` 0,
`fit_max_speed` 1.2, its run braked for a wall (`--wall_brake=0`: as before) and its `FellowJointLimits3D`'s
`hinge_max_turn` 175°; `tools/effort.py` measures the forces; milestone 204 (decision 0346): with `leap_push_force`
the push as long as that force needs for the dead point (up to 0.65 m) and the pose's hips held off the body from the
push to the dead point; with `climb_pull_speed` a foot stepping up or across the wall no higher than 0.25 m over its
hanging place under the body, the trailing hand across over 0.6-0.95 of the move; a hanging foot's way in and out from
the wall at most 1.5 m/s; the showcase's player at an arcade's pace — `leap_push_force` 5.5 (the 2 m gaps leapt),
`climb_pull_speed` 2, no step up, its neck limited to −66..56° (`effort.py --profile arcade`); milestone 205 (decision
0347): with `leap_push_force` an eave's lip out over the hold (0.3 m at least) is leapt to; with `climb_pull_speed`, up
from a hang onto a ledge the mantle's clip no faster than its rise at 1.25 times that speed, the feet on the wall found
under the edge or hanging free, the climb left at its own pace (not `exit_speed`); the showcase's player looks no
steeper than 50° (from its head, at least 3 m out), bends its neck sideways 15° at most and walks up roofs to 28°
(`CRAWL_SLOPE`), leaning into them; milestone 206 (decision 0348): with `leap_push_force` a leap to a lip standing out over
the hold left by 0.2 m or more takes the hands round it (out at the hold's height, up past its front, onto its top
from above); the showcase's jump at a wall is the mannequin's own clip (`MOCAP_KEEP_OWN`), blended over 0.45 s;
milestone 207 (decision 0349): `hang_side_dead_zone` (0: off; hanging, a wish pushing in or up whose part along the edge
is under that share of its length goes straight in or up — the intent is a speed and the hang's thresholds absolute, so
a stick at the wall 7° off square had read as along the edge; the showcase 0.5, 30°); the showcase's fall the clip
"fall" (ready to land) blended into "fall_flail" from 0.45 to 1.1 s in the air and back within 0.3 s of the ground
(`FALL_FLAIL_FROM`, `FALL_FLAIL_FULL`, `FALL_READY`), a jump pressed while hanging the traversal's alone; `FellowFootLock3D`: a foot let go is never eased back from further
from the hip than the leg reaches; `release_extension` (milestone 172; 1.0: off): let go when holding the foot needs more
than this share of the leg's length. Let go without jumps (milestone 173, decision 0315): a held point out of
the leg's reach is let go from where the foot is shown and brought within reach the shorter way (towards the animated
foot, or straight towards the hip); the knee bends towards the foot's forward where the animated leg is straight.
Bench `bench/climb_check.gd` (`hold_climb`, `side_reach`, `narrow_holds`, `eave_roof`, `eave_deep`, `beside_reach`,
`stacked_holds`, `parapet`, `leap_style`;
`--only=<lanes> --times=<s,...>` for a picture strip of some lanes at given times).

Faces (milestone 83, decision 0191): `FellowFaceAnimator` (a `Node`; nothing changes without it): `mesh_path` (the
face's `MeshInstance3D`), `voice_path` (an `AudioStreamPlayer`, 2D or 3D), `voice_stream` and `voice_time` (−1: with a
time, the stream read there instead), `blend_shapes` (channel → blend shape names; ARKit's by default). Lip sync:
`lip_sync` (on), `strength` (1), `anticipation` (0.04 s), `attack` (0.04 s), `release` (0.08 s), `level_floor_db`
(−45), `level_full_db` (−18), `max_analysis_seconds` (60). Blinks: `blinks` (on), `blink_interval_min` (2 s),
`blink_interval_max` (6 s), `blink_duration` (0.18 s); `seed`. `blink()`, `get_channel()`, `get_channels()`,
`sample_voice()`, `get_stats()`, `get_global_stats()`/`reset_global_stats()`, `clear_analysis_cache()`; `CHANNEL_*`
constants. Bench `bench/face_check.gd` (`--listen=1` under Movie Maker, `--many=N`); the regression scene `face`.
Gaze (milestone 89, decision 0197): `gaze` (on), `look_target_path` (empty: ahead), `eye_center` (in the face mesh's
space), `gaze_range_horizontal` (30°), `gaze_range_vertical` (25°), `gaze_time` (0.04 s), `saccades` (on),
`saccade_amplitude` (1.5°); `get_gaze_angles()`; the channels `look_out_left` … `look_down_right` on ARKit's
`eyeLook*` shapes. Vergence (milestone 94, decision 0202): `eye_spacing` (0 m: the eyes parallel); `get_vergence()`.
Expressions (milestone 100, decision 0208): `set_expression(name, weight = 1, fade = 0.3)`, `clear_expressions(fade)`,
`get_expression_weight()`, `get_expression_names()`; built in `happy`, `sad`, `angry`, `surprised`, `afraid`,
`disgusted`; `expressions` (the game's own: name → {channel: weight}); the channels `brow_inner_up`,
`brow_down_left/right`, `brow_outer_up_left/right`, `smile_left/right`, `frown_left/right`, `cheek_squint_left/right`,
`eye_squint_left/right`, `eye_wide_left/right`, `nose_sneer_left/right` (ARKit's `browInnerUp` … `noseSneerRight`).
`speech_brows` (0.5: the brows on the voice's stressed syllables), `speech_emphasis_db` (6 dB); `get_emphasis()`,
the signal `emphasized(strength)`; milestone 104: `get_emphasis_count()`, `get_last_emphasis_strength()`,
`get_speech_activity()` (0-1).
The head (milestone 104, decision 0212): `FellowHeadMotion3D` (a `SkeletonModifier3D` under the character's skeleton):
`face_animator_path`, `head_bone` (`Head`), `neck_bone` (`Neck`), `neck_share` (0.4), `forward` (+Z, the skeleton's
space); nods `nod_degrees` (6°) at each stressed syllable, `nod_time` (0.12 s); sway `idle_sway_degrees` (0.5°),
`speech_sway_degrees` (2°, × the speech activity), `sway_frequency` (0.3 Hz), `seed`; `nod(strength)`,
`get_head_angles()`, `get_stats()`. Bench `bench/face_check.gd` (`--heads=1` with `--many`). Towards what the face looks
at (milestone 149, decision 0278): `follow_gaze` (off), `gaze_dead_zone` (10°: the eyes alone), `gaze_follow` (0.7 of
the angle past it), `max_head_yaw` (70°), `max_head_pitch` (35°), `gaze_follow_time` (0.3 s); `get_gaze_turn()`; the
face told (`FellowFaceAnimator.set_head_turn()`, `get_head_turn()`), its eyes turning the rest.

## Crash reports (milestone 97)

Decision 0205. `debug/fellow/crash_reports/enabled` (off; read at start-up; not in the editor, and off with
`rendering/fellow/enabled` off): a crash writes a JSON report — in a release template too — into
`debug/fellow/crash_reports/directory` (`user://crash_reports`): the reason (signal or exception, code, faulting
address), the time, uptime, frame, thread and scene, the memory, the machine (engine and commit, build, OS, CPU, RAM,
GPU and driver, renderer, project and version), the faulting thread's frames (module and offset), the breadcrumbs, the
custom values, the last `debug/fellow/crash_reports/log_lines` (200) lines of the log and GDScript's backtrace (in a
release build with `debug/settings/gdscript/always_track_call_stacks`); on Windows a minidump beside it
(`debug/fellow/crash_reports/minidumps`, on). The newest `debug/fellow/crash_reports/max_reports` (10) are kept.
Godot's own handler and any earlier one still run afterwards; the process ends as it would have.
`FellowCrashReporter` (a singleton): `add_breadcrumb(message, category)`, `set_custom_value(key, value)`,
`remove_custom_value()`, `get_custom_values()`, `write_report(reason)` (a report without crashing),
`get_pending_report()` (an earlier run's newest unacknowledged report), `acknowledge_reports()`, `get_reports()`,
`read_report(path)`, `is_enabled()`, `get_report_directory()`, `get_stats()`, `debug_crash(kind)` (`segfault`,
`abort`, `trap`, `stack_overflow`, `thread`, `divide`). `tools/fellow_crash/symbolize.py` prints a report with its
frames symbolized (the build's symbols); `tools/fellow_crash/crash_check.py` checks a build.
Hang reports (milestone 98, decision 0206): `debug/fellow/crash_reports/hang_seconds` (10 s; 0 off; read at start-up):
a watchdog writes `hang_*.json` when the main thread has made no progress that long — `stalled_ms`, the main thread's
frames, on Linux and macOS its `script_frames`, on Windows a minidump — and appends `recovered_after_ms` when it goes
on; not with a debugger attached or while the machine sleeps.
Uploads (milestone 102, decision 0210): `upload_reports(url = "")` sends the waiting reports to `url` or
`debug/fellow/crash_reports/upload_url` (empty) on a thread, oldest first — a POST of `multipart/form-data` each:
`report` (the JSON), `upload_file_minidump` (its minidump, with `debug/fellow/crash_reports/upload_minidumps`, on),
`kind`, `reason`, `project`, `version`, `commit`; accepted (2xx) ones moved into `sent/`, the rest kept;
`report_uploaded(path, ok, response_code)` (negative: −1 the URL, −2 the host not found, −3 no connection, −4 the TLS
handshake, −5 no response; a collector not reached is not tried again for the rest), `uploads_finished(sent, failed)`,
`is_uploading()`.
`debug/fellow/crash_reports/upload_automatically` (off; read at start-up): sent on their own two frames after the start.

## Hitch sampler (milestone 117)

Decision 0231. `rendering/fellow/debug/hitch_sampler` (off; read at the first frame): a watchdog thread follows the main
thread's frame steps (`Main::iteration()` up to the frame delay) and, with Godot's separate render thread, its draws.
Once a step has run for half of `rendering/fellow/debug/hitch_sampler_ms` (33.3 ms), the stepping thread's call stack is
sampled about every millisecond, with the CPU time it got, and every other thread's stack every fourth sample. A step
longer than the threshold is a hitch; up to 64 are reported when the engine exits (printed): the step's length, when,
the share of it the thread was on a CPU (none: waiting or not scheduled), the hottest call path, the innermost
functions, and the other threads that were busy or not waiting (a thread holding what the step waits for may not be
running either) with theirs. Windows x86-64: stacks copied while the thread is suspended and unwound from the images'
unwind tables; names from a PDB, or from a linker map beside the executable (`scons ... linkflags=/MAP`). Linux: a
signal; names from the executable's symbol table (`FELLOW_KEEP_SYMBOLS=1` at link: not stripped). FellowEngine's
statistics count `hitches` and `hitch_samples`. Changes no frame; elsewhere it does nothing.

The GPU's side (milestone 119, decision 0237; every platform): with the sampler on, the renderer's pass timestamps are
captured every frame (as with `profiling/pass_timestamps`). A frame whose passes took the GPU longer than the threshold
(its first timestamp to its last), or whose first timestamp came that long after the previous frame's last (work the
GPU did outside the frame's passes — a transfer worker's copies, another program — beyond the CPU's own time between
recording the two frames, which leaves the GPU idle: the CPU's hitch, not the GPU's), is a GPU hitch: up to 64 are reported after the CPU's, each with when it was recorded, its passes' time and
their average over ordinary frames, the gap before it and its average, and the six passes longest past their averages.
FellowEngine's statistics count `gpu_hitches`.

In the editor (milestone 119, decision 0238): the debugger's **FellowEngine Hitches** tab lists a game's hitches as they
happen (when, CPU or GPU, how long, where) with the exit report's text for the one selected; **Record** starts the
sampler in the running game (and in the next one) at the tab's threshold, without the project setting. In the game, a
CPU hitch is named on a low-priority thread of its own; the messages are sent by the main thread. Milestone 120
(decision 0242): a GPU hitch selected draws its frame's passes on a time line, a row per queue, each reddening as it runs
past its average (tooltips give the length, start and average). Milestone 121 (decision 0243): **Capture GPU Frame**
asks the running game for its next frame's passes, with the sampler on or off: the game captures the renderer's pass
timestamps until a frame recorded after the request is read back, and the tab lists it as a `Frame` row, selected, its
passes longest first and on the time line. Nothing is captured when the button is not pressed. Decision 0244:
**Record GPU Frames** records every frame of the next seconds (the spin box beside it; up to 30 s, 3 000 frames),
sent in batches of 30 as they are read back: a `Frames` row whose graph draws a bar a frame — the time since the frame
before (grey) behind the GPU's passes (blue), lines at 16.7 and 33.3 ms — its slowest frame selected; clicking or
dragging over the graph shows a frame's passes on the time line and in the text (the CPU's recording of them and its
wait for the GPU before it too).

Script frames (milestone 119, decision 0239; GDScript's debug builds: the editor, games run from it, debug templates):
each sample of a stepping thread also reads its GDScript call levels, and a hitch's report adds the script call path
and innermost script functions with their lines (`_sleepy_function (res://main.gd:25)`); the editor's list shows the
innermost one of the hottest path.

## Simulated GPUs (milestone 49)

The environment's `FELLOW_VULKAN_PROFILE=<name>` (read once by the Vulkan driver; a diagnostic, nothing changes
without it) makes the device present itself as another: `android_2022`, `android_2025` (Android's baseline
profiles), `intel_uhd620`, `intel_arc_a770`, `gtx1060`, `rtx4070`, `rx580`, `rx7800xt`, `apple_m1` (MoltenVK), or
`no_ray_tracing`, `no_float16`, `subgroup_basic`. Extensions, features, limits, subgroup operations and compressed
formats the other device lacks are hidden; every shader is checked against it, and what it could not run is printed
once a pipeline is made from it, as `FELLOW_PORTABILITY <profile>: ...`. An unknown name prints the known ones and
leaves the device as it is. Decision 0150; bench `tools/platforms/` (`renderer_matrix.sh`, `profile_matrix.sh`,
`platforms.sh` for the Windows and arm64 builds under Wine and qemu, and from milestone 50 Android's and the web's;
`web/web_check.sh` runs the web template in headless Chromium).

## Editor support (milestone 50)

Module `fellow_editor` (editor builds only; nothing in a game): gizmos for `FellowGasVolume3D` (its box, with handles),
`FellowGasEmitter3D`, `FellowWater3D` (its `extent`), `FellowShallowWater3D` (its domain), `FellowStreamingCell3D`
(its bounds and own load distance), `FellowWind3D` (an arrow along −Z), `FellowLightning3D` (its distance band) and
`FellowSnowTracker3D`, and an icon billboard for every FellowEngine node; `.bvh` files imported as `Animation`
resources (import options `scale`, `skip_first_frame`; presets for centimetres and the CMU conversion); Project >
Tools > **Split into Streaming Cells** for the selected `FellowWorldStreamer3D`. Performance monitors (the debugger's
Monitors tab, *FellowEngine*; in every build): `Texture streaming resident` / `wanted` (bytes), `pending`; `World
cells loaded` / `loading`; `Motion matching time` (CPU seconds a frame, summed over the threads) / `updates` (a
frame); milestone 114: `Video memory (device)` (bytes in the GPU's own heaps; Godot's video memory monitor counts the
host memory the driver allocates too) and `Upload staging` (RenderingDevice's transfer workers' staging buffers, host
memory: decision 0225); milestone 120: `Video memory budget` and `Video memory used (driver)` (the operating system's
budget for the device's own heaps and the process's usage of them, decision 0240) and `Texture streaming budget` (the
budget in force). Decision 0151; check `benchmarks/tools/editor_check/run_editor_check.py`.

## The editor: animate or still, warnings, handles (milestone 52)

In the editor FellowEngine's time-driven nodes animate as in the game (the water's waves, rain, lightning, wind, gas
and shallow water), and ask for frames only for what moves: nothing they give the renderer is written again unchanged
(lightning between strikes, rain's drops, the water's clock), simulations ask for a frame per step (gas while it emits
and 20 s after). The 3D editor's toolbar toggle **Animate** — the editor setting `editors/3d/fellow/animate_nodes` (on)
— stills them: the editor then redraws only when something changes. Games always animate. The nodes' configuration
warnings (the Scene dock's icon) say when one cannot work where it is; `FellowEditorTools.get_configuration_warnings()`
returns them to scripts and fellow_agent's `check` reports them. Handles for emitters, water extent, shallow-water
domain, cell bounds, snow tracker, lightning distances (undoable). Every `rendering/fellow/*` setting has a tooltip
(`doc/tools/fellow_settings_docs.py`). Decision 0153; check `benchmarks/tools/editor_check/run_editor_check.py`
(`--perf <scene> --perf-idle [--perf-still]` measures the idle editor).

## The editor: scene opening, ready-made setups (milestone 53)

FellowEngine's nodes share what they make from code — shaders, compute kernels, the water's grid and foam texture —
once per session (`modules/fellow_fluid/fellow_shared_shaders.h`): opening a scene again, or adding another instance,
compiles nothing. The 3D editor's toolbar has a **FellowEngine** menu: *Add Weather* (a FellowWeather3D and the rain,
wind and lightning the scene lacks), *Add Fire*, *Add Smoke*, *Add Burning Box*, *Add Ocean*, *Add Lake (20 x 20 m)*,
*Add Flowing Pool (8 x 8 m)* — added to the selected 3D node (or the scene's root), undoable, with no configuration
warning. Decision 0154.

## Image regression suite (milestone 54)

`tools/fellow_regression/regression.py run|bless` renders each group of FellowEngine's features (the defaults,
FellowEngine off, post-processing, ray tracing) on lavapipe and fails on a picture that differs from its golden, an
error, a release template drawing differently from the editor build (`--template`), or FellowEngine's defaults leaving
stock's frame. Exact on lavapipe; `--goldens DIR` keeps another GPU's own. No engine setting. In the editor, a
FellowGasVolume3D's gizmo and handles now show its box as it is, its floor on the node, and an emitter that does not
reach into its volume's box has a configuration warning. Decision 0155; `tools/fellow_regression/README.md`.

## Agent toolkit (milestone 51)

`tools/fellow_agent/fellow_agent.py capture|describe|check|run|compare|import|api|init` (Python 3; JSON out, exit
codes 0, 1, 2): a scene rendered to PNGs from its camera or framed views with statistics and errors, its tree as
data, a project's scenes and scripts checked, frame times measured, two renders compared, the class reference
printed, an `AGENTS.md` written for a game project. No engine setting; `AGENTS.md` at the root for agents working on
the engine. Decision 0152; `tools/fellow_agent/README.md`.

## Terrain (milestone 57)

A `FellowTerrain3D` node (module `fellow_world`) draws `heightmap` (an `Image` of heights in metres, `FORMAT_RF`, one
texel a vertex `vertex_spacing` apart; `heightmap_from_image()` converts a grey image) with continuous levels of
detail: a quadtree of patches of `patch_size` quads (16) whose vertices morph onto the next coarser grid before it
takes over, the finest level to `lod_distance` (0: three times its node's size), each further level twice as far
(`morph_ratio` 0.7: where the morph begins). The patches are one MultiMesh, written into a second hidden one when the
selection changes and shown three drawn frames later (motion vectors of a static ground); FellowEngine's MultiMesh
culling culls them one by one. Up to 9 `layers` (`FellowTerrainLayer`: albedo and normal textures every
`texture_size` metres, `triplanar`, `albedo_color` in sRGB, roughness, `height_in_albedo_alpha`): layer 0 the base,
`splat_map` and `splat_map_2` the weights of layers 1-4 and 5-8; `height_blend` (0.3), `macro_variation` (0.15).
`collision_enabled` (on): height map shapes of `collision_region_size` cells (256), the finest level's triangles.
`get_height()`, `get_normal()`, `get_layer_weights()`; `update_heightmap()` after editing the images in place;
`debug_view` colours the levels of detail. FellowEngine's weather (wetness, snow cover) reaches it as any material. In
the editor the levels follow the 3D view's camera. Nothing changes without the node. Decision 0158; bench
`bench/terrain_check.gd`, Slice Lab `--slice_terrain=node`.

Its `foliage` (`FellowTerrainFoliage`, decision 0159): a `mesh` scattered over a `layer` by `density` per square metre
where its weight is 1 (none below `min_weight`), within `slope_min`/`slope_max` and `height_min`/`height_max`, scaled
(`scale_min`, `scale_max`), turned (`random_yaw`), tilted (`align_to_normal`), sunk (`sink`), up to `range` from the
camera in cells of `cell_size` (0: a quarter of the range), each cell made once on a worker thread
(`foliage_cells_per_frame`, 8, started a frame) into a MultiMesh of its own whose instances the GPU culls by distance;
`cast_shadows`, `seed`. Slice Lab `--slice_foliage=node`; `terrain_check.gd --foliage=1`.

Editing (milestone 58, decision 0160): `sculpt(global_position, radius, strength, mode, target_height)` — `SCULPT_RAISE`
and `SCULPT_LOWER` by `strength` metres at the brush's centre, `SCULPT_SMOOTH` and `SCULPT_FLATTEN` (towards
`target_height`) by `strength` of the way — and `paint(global_position, radius, layer, strength)` (the weights still add
up to 1; the splat maps are made, RGBA8, when there are none) change only the region under the brush: the queries at
once, the textures and collision shapes at the frame's end (once for the frame's edits), the foliage over it scattered
again; the result is the terrain a rebuild from the edited images makes, texel for texel. `blit_heightmap_region()` and
`blit_splat_region()` write a rectangle back (undo; stamps). In the editor, a selected terrain's toolbar in the 3D view
(Raise, Lower, Smooth, Flatten, Paint; the layer, the brush's size and strength; Shift inverts Raise and Lower) sculpts
and paints with the mouse, one undoable action a stroke. Bench `bench/terrain_edit_check.gd`; the editor check's
terrain brush.

Milestone 64 (decision 0172). **Holes**: `hole_map` (an `Image` of the heightmap's size, a texel above half a hole at
that vertex; none by default): a triangle touching a hole's vertex is not drawn (the patches over holes are drawn with
a second material that discards it, the rest of the terrain keeps the material without a discard), has NaN heights in
the collision shapes (no collision with Jolt, Godot's 3D physics) and no foliage; `get_height()` returns NaN there,
`is_hole()` true. `paint_holes(global_position, radius, hole)` cuts or fills with a brush and updates only the region,
`blit_hole_region()` writes a rectangle back; the editor's toolbar has a Holes tool (Shift fills). **Foliage fading
out**: `FellowTerrainFoliage.fade_distance` (metres, 0: they stop at the range, as before): over its last metres before
`range` the instances shrink to nothing, scaled one by one by FellowEngine's MultiMesh culling on the GPU (the renderer's
`fellow_instance_set_instance_range_fade()`). **Impostors baked into files**: `FellowTerrainFoliage.impostor` (a
`Mesh`) and `impostor_distance` (0: none): the meshes up to the distance, the impostors from it to the range, each
instance on the GPU, the shadows per directional cascade as an HLOD's; needs `geometry/multimesh_culling`.
`FellowTerrain3D.bake_foliage_impostors(directory, frames, resolution)` renders each kind with a distance from
`frames` × `frames` directions (octahedral, decision 0122's baker), saves it as `.res` (the quad mesh, its material,
its atlases lossless) and sets it; signal `foliage_impostors_baked`. In the editor: the terrain toolbar's Bake
Impostors button (next to the scene, `impostors/`, undoable). Bench `bench/terrain_holes_check.gd`. Milestone 133
(decision 0257): `impostor_fade` (metres, 0: a switch): each instance's mesh and impostor dithered into each other over
that on each side of `impostor_distance`, on the GPU (the renderer's `fellow_instance_set_instance_range_margins()`);
resolved by TAA or FSR.

Milestone 65 (decision 0173), **regions**: `region_group` (empty: a terrain on its own) makes terrains of one group,
the same spacing, edge to edge, tiles of one height field — their border vertices drawn from the exact heights, their
border normals from the neighbours' heights, the layers' textures and macro variation in the group's space, the brushes
carried across (the shared edges made equal; the editor's undo covers every region a stroke changed), the foliage on the
group's grid. `split_into_regions(region_quads, directory, streamer)` cuts a terrain into such regions (a power-of-two
multiple of `patch_size` quads, padded with holes past the map), each a `FellowStreamingCell3D` of `streamer`; the
streamer's `split_into_cells()` does it for terrains with the option `split_terrains` (the editor's Split into
Streaming Cells). A region's data is built on the streaming worker thread. Bench `bench/terrain_regions_check.gd`.

Milestone 70 (decision 0178), **virtual texture**: `virtual_texture` (off) draws the terrain's material from pages
baked once into a virtual texture instead of blending its layers in every fragment. Pages of 128 × 128 texels,
`virtual_texture_texels_per_meter` (64) at the finest mip and each mip's texels twice as wide, are baked by a compute
kernel with the layers' own arithmetic (the textures prefiltered by each texel's footprint) into the slots of an atlas
every terrain shares — `rendering/fellow/terrain/virtual_texture_atlas_size` (4080 texels: 30 × 30 slots of 136 with
their borders; two RGBA8 textures, 127 MiB) — and found through the terrain's page table; the coarsest mip is the
terrain's overview (128 × 128 with its own mips, 175 KB), read far away and wherever no page is baked yet. The pages a
camera wants (by its distance to them; outside the frustum coarser) are chosen on the CPU each frame and the missing
ones baked within `rendering/fellow/terrain/virtual_texture_pages_per_frame` (16) for every terrain, coarsest and nearest
first, the least recently wanted giving their slots up; with more than 90 % of the slots wanted, every terrain's pages
get coarser (up to four mips) until under 60 %. `virtual_texture_lod_bias` (0; positive: coarser pages). Sculpting,
painting, blits and changes to the layers bake the pages over them again. `debug_view` Virtual Texture colours the mip
drawn; `get_virtual_texture_stats()`. Regions have their own pages in the shared atlas. Needs the RenderingDevice
(Forward+, Mobile; Compatibility draws the layers, with a configuration warning). The layers' colours reach the shader
converted from sRGB once (milestones 67-69 converted them twice: darker than their colour). Bench
`bench/terrain_vt_check.gd`; the regression scene `terrain_vt`.

Milestone 71 (decision 0179): `rendering/fellow/terrain/virtual_texture_all` (off) draws every `FellowTerrain3D` from
its pages as if its `virtual_texture` were on (to compare a project with and without; read as a terrain enters the
tree). Measured on the RTX in Frontier Lab: the GPU's frame 5-10 % shorter at the still cameras (1080p) and 5 % in the
flight at 4K; where the frame is the CPU's, no change (the page choice 0.08-0.14 ms a frame of the main thread for 66
regions). A terrain keeps its page choice while its view barely changes (the choice again every 4 frames near the
camera, 8 past 256 m, 16 past 1 km, or when it moved, pages are missing or stale). `get_virtual_texture_stats()`:
`updates`, `visits`, `atlas_wanted`. Counters: `terrain_vt_update_usec`, `terrain_vt_setup_usec`. Frontier Lab:
`--frontier_vt=1`.

`FellowGasVolume3D` no longer reads the data of concave, height map and custom shapes in its box (not its obstacles),
and the directional shadow cache ignores changes of instances that cast no shadows (both exact).

## Pipelines compiled ahead (milestone 59)

`rendering/fellow/frame/stable_scene_bits` (on, exact; decision 0161): the colour passes' specialization bits that
follow what a frame holds — decision 0057's absent decals, reflection probes and lights, the positional lights' loop
variants and tile culling, the weather's wetness, the clouds' shadow — keep the feature's code once the feature exists
(a hidden light counts) or has been in a frame, so that a light switched on, the rain starting or a lightning flash
no longer asks for new pipelines for every material in view. `FELLOW_SPECIALIZATION_LOG=1` (environment) prints each
specialized pipeline a draw asked for before it was compiled, with its key's bits; `bench/pipeline_states_check.gd`.

`FellowPipelineWarmer3D` (decision 0162): while a scene loads, draws its world from the camera's place in the six
directions of a cube into a small viewport set up as the game's (MSAA, TAA, scaling, shadow atlas, the camera's
environment and layers), waits until nothing more compiles and emits `finished` — the materials around no longer
compile their pipelines when the camera first turns to them. `autostart`, `from_camera`, `view_size` (128 px),
`frames_per_view` (3), `settle_frames` (15), `max_frames` (900); `start()`, `is_finished()`, `get_stats()`. The
FellowEngine menu's "Add Pipeline Warm-up"; Slice Lab `--slice_warmup=node`; `bench/pipeline_warmup_check.gd`.

Milestone 61 (decision 0167): `stable_scene_bits` also keeps Godot's directional soft-shadow bit and the first
directional light's traced and contact shadow masks' bits in frames without a directional light (the sun set: the
directional loops run no iteration), and light tile culling's bit in frames without omni and spot lights only while
tile culling is on (autotune's choice off: the frames with and without lights share pipelines). The warmer's `shelf`
(on): after the six directions it draws, from a view 4 km above, a copy of each distinct mesh and material set of the
world's visible `MeshInstance3D`, `MultiMeshInstance3D` and `FellowTerrain3D` foliage (36 a page, at most 576; the
copies' 40 m visibility range hides them from every other camera, and each follows its source's mesh and materials
while shown); `get_stats()` adds `shelf_items`, `shelf_pages`; Slice Lab `--slice_warmup_shelf=0` leaves it out.

`rendering/fellow/frame/kernel_spirv_cache` (on, exact; decision 0169): FellowEngine nodes' compute kernels (water,
rain, snow tracks, shallow water) kept as SPIR-V in `user://fellow_kernel_cache` after their first compilation, named
by their source and the engine's build. An export with Godot's shader baker (`shader_baker/enabled`) bakes FellowEngine's
effects and the materials its nodes make from code too (they are made when the export starts; a module registers its
codes with `FellowBakeShaders::register_provider()`, `servers/rendering/fellow_bake_shaders.h`). `fluid/gas_mg_fine_sweeps` (2): the gas multigrid's sweeps before
and after the finest level's correction (1 halves that level's smoothing, not exact: on the RTX the slice's pressure
solve 0.65 → 0.57 ms, its residual 20 times larger; milestone 61).

Milestone 63 (decision 0171), **recorded pipelines**: `rendering/fellow/pipelines/record` (off) writes the pipelines
the scene shaders (Forward+) compile while the game runs — each under a hash of its material shader's code, its key's
vertex and framebuffer formats described — to `rendering/fellow/pipelines/path` (`res://fellow_pipelines.json`, merged
with what is there, written three seconds after something new and at the end; not in the editor itself; `user://` when
an exported game cannot write `res://`). `rendering/fellow/pipelines/replay` (on; nothing without the file): when a
shader is made at load, its recorded pipelines compile in the background before a draw asks for them. A file recorded
with another scene shader (its source and the rendering settings' defines) is not replayed; recording starts it again.
Play the game through its weathers, lights and places once with `record` on, keep the file in the project, turn
`record` off. All three are read when the renderer starts; `FELLOW_PIPELINES_RECORD`, `FELLOW_PIPELINES_REPLAY`
(0/1) and `FELLOW_PIPELINES_PATH` override them (tests), `FELLOW_PIPELINES_DEBUG` names the pipelines not kept;
`RenderingServer.fellow_get_stats()` counts `pipelines_recorded`, `pipelines_replayed`. The shader baker also asks
each node of an exported scene for the shaders it makes from its setup (`FellowBakeShaders::register_object_provider()`:
a burnable's burning materials).

## Rooms behind windows (milestone 188)

`FellowInteriorMaterial3D` (module `fellow_world`; decision 0330) is a material for windows' glass that shows a room
behind each window without its geometry (interior mapping): the view's ray followed into a box behind the glass —
walls (plaster or paper, panelling, a skirting and a cornice; a painting, a door, a bookcase or a wardrobe on the back
wall, pictures on the side walls), a floor (cotto, boards, parquet or stone, a rug), a ceiling (beams or plaster),
furniture on two planes across the room, a hanging lamp, curtains just behind the glass (none behind a shop's window
or a door's glass: the glass's vertex colour's alpha under 0.5) — lit by the day through the window and, in some rooms,
their lamp, seen through glass with Fresnel's law, grime, the sky's reflection and old panes out of true. Every
pattern is filtered over the pixel's footprint (no flicker under TAA). One shader for every instance, made once and
baked with the project's shaders.

| property | default | what it does |
|---|---|---|
| `rooms_from_uv2` | on | each window its own room, centred on it, from the glass's UV2 (the metres right of the window's middle and up from its room's floor); off: a grid of rooms `room_size` apart along the facade in the mesh's space, from `floor_height` |
| `room_size` | (3.6, 3.6, 4.5) | a room's width, height and depth (m) |
| `seed` | 0 | another set of rooms for the same windows |
| `daylight`, `daylight_color` | 1.0, warm white | the rooms' light by day near the window (falling off to a tenth at the back wall); lower it at dusk, to about 0.05 at night |
| `lamp_share`, `lamp_energy`, `lamp_color` | 0, 3, warm | the share of rooms whose hanging lamp is lit, and its light |
| `curtain_share` | 0.6 | the share of windows with curtains (six in ten of them drapes, four nets) |
| `furniture` | 1 | how furnished the rooms are (0: empty) |
| `glass_tint`, `glass_roughness`, `grime` | pale green-grey, 0.08, 0.25 | the glass's colour, its roughness, its dirt (dimmer, rougher patches) |
| `reflection` | 0.35 | how much of the sky's reflection the glass keeps (a street's other houses hide most of it) |
| `pane_size` | (0, 0) | the panes' size (m): each pane's normal a little out of true; 0: flat glass |

The showcase's `tools/window_uv2.gd` gives a mesh's glass its UV2 (each connected piece a window, in its facade's
frame; a tall window's room floor at its bottom and its vertex alpha 0) — the rooftops' houses use it. The RTX at 1080p:
no measurable cost against the dark glass it replaced.

## Weathering (milestone 190)

`FellowWeathering3D` (module `fellow_fluid`; decision 0332) weathers every opaque surface of the instances in its
`cull_mask` as it is drawn (Forward+), from the scene's shape — no textures, no painting: a patina (the albedo's tone
over metres); streaks hanging from the nearest ledge above a wall's point (a sill, a hood, a cornice, a balcony, an
eave), each lane its own width, length and darkness, darker under a ledge's ends where the water runs off, none under a
mass that rises on (a recess, a jetty); damp at a wall's foot up to a ragged tide line with salt above it and the
splashes' grime below. It finds the ledges and the ground in a stack of heightfields around the camera — the scene seen
from above in horizontal layers, each layer's highest surface — so a sill is seen under the cornice above it. Albedo
only; every pattern fades to its mean where it is finer than a pixel. One node at a time; nothing changes without it.

| property | default | what it does |
|---|---|---|
| `strength` | 1 | the whole weathering (0: none) |
| `streaks`, `streak_length`, `stain_color` | 1, 1.5 m, (0.56, 0.53, 0.48) | the streaks' strength, how far below a ledge they fade (each lane 0.4-1.6 times it), what a full streak multiplies the albedo by |
| `damp`, `damp_height`, `damp_color` | 1, 0.8 m, (0.6, 0.6, 0.52) | the damp's strength, its tide line's height above the ground (0.75-1.25 times it along the wall), what full damp multiplies the albedo by |
| `patina` | 0.5 | the albedo's tone varying over metres |
| `seed` | 0 | moves every pattern |
| `cull_mask` | all 20 layers | the render layers weathered and drawn into the layers (keep characters and moving things out) |
| `heightfield_size` | 64 m | the layers' width around the camera (moved a sixteenth of it at a time, one layer a frame; all at once after a cut) |
| `heightfield_layers`, `heightfield_layer_height` | 16, 1.75 m | the stack up from the node's height: about half a storey a layer keeps a window's sill and the hood over it apart |
| `heightfield_resolution` | 1024 | each layer's texels across `heightfield_size` (4 bytes each: 64 MB for sixteen at 1024) |
| `heightfield_always_update` | off | the layers drawn every frame (geometry that moves) |

The showcase's rooftops weather their buildings, fountain, pavements and paving (render layer 5); `tools/weathering_look.gd`
looks at it with the node on and off (milestone 199: and from the bell tower — `vista_s`, `vista_w`, `vista_e` —,
`--timing=1` each view's GPU and CPU time over 120 frames, `--node`, `--env`, `--mat` and `--setting` a node's, the
environment's, a material's or a project setting's value for the run).

## Options that change the image (off by default)

| setting | what it does | image cost | record |
|---|---|---|---|
| `geometry/multimesh_instance_lod` (with `geometry/multimesh_culling`) | each MultiMesh instance gets the mesh LOD level stock gives a node of its own (distance to its box, the project's `mesh_lod_threshold`), instead of the level of the MultiMesh's whole box | not visually lossless where many far instances coarsen together: Foliage Lab from above 33-40 dB (far grass clumps lose blades); at ground level 48.2-64.6 dB; −2.2 % with the bench's light LOD meshes | 0035 |
| `geometry/multimesh_cull_vertex_motion` (with `geometry/multimesh_culling`) | MultiMeshes whose material moves vertices (wind) are culled too when their node has an extra cull margin: the margin is taken as how far the vertices move | none while the material keeps within the node's margin (Foliage Lab with wind: 0 differing pixels); beyond it, instances near the view's edges can be lost | 0036 |
| `geometry/contribution_cull_pixels` (0 = off) | contribution culling: the camera does not draw a mesh or MultiMesh node whose bounding sphere would cover fewer than this many pixels of the viewport's width (as `mesh_lod_threshold`), nor, with `geometry/multimesh_culling`, such a MultiMesh instance; nodes with `ignore_occlusion_culling` are always drawn | tiny far objects disappear: City Lab at 2 px of 960 (about 4 px at 1080p) 62.9-78.4 dB, 292 pixels over three captures; 5-14 % of the city's nodes not drawn, 10-13 % fewer primitives | 0121 |
| `geometry/contribution_cull_instances` (Thin) | MultiMesh instances below the threshold: thinned (a share kept and grown to the threshold's size, so a far meadow keeps its coverage) or culled | Foliage Lab aerial at 2 px of 960: thinned 41.1 dB with the meadow's mean colour unchanged; culled 36.8 dB, the meadow 3.7 levels lighter | 0121 |
| `shadows/contribution_cull_texels` (0 = off) | a caster whose box's diagonal spans fewer than this many texels of a directional cascade is not drawn into it (per node; per instance for culled MultiMeshes) | the shadows of tiny far casters disappear: City Lab at 2 texels 0 differing pixels (its smallest casters span 6 texels of the far cascade) | 0121 |
| `geometry/dithered_fade` | an opaque instance fading at its visibility range (its own fade, or its visibility parent's) is drawn in the depth pre-pass and the opaque passes with a share of its pixels kept by a dither moving with TAA's frames, instead of blended in the transparent pass; one fading at its range's start keeps the other share (two levels crossing over cover every pixel once); their shadows dithered the same way in the shadow maps, from milestone 135 (decision 0260: an omni light's cube map casts them whole while fading); Forward+ | fading objects stay solid instead of see-through, with TAA or FSR a blend over a few frames, without them a screen-door pattern; 4 000 balls all mid-fade on the RTX: GPU +0.9-1.7 ms against blending, the renderer's CPU −0.7 ms (instanced); City Lab's HLOD fading: the same GPU time as blended | 0256, 0260 |
| `geometry/cluster_lod` (with `geometry/mesh_clusters`) | each cluster of a large static surface is drawn at the mesh LOD stock would give it as an instance of its own (the main camera's distance to the cluster), with its neighbours' finer levels where they meet (no cracks) | the image as LOD changes it: Sponza 63.6 dB on a moving sequence, stills 52.9-56.8 dB at distant silhouettes; Sponza GPU −3.1 %, render thread +0.21 ms | 0063 |

## Exact options (on by default unless noted)

| area | settings | record |
|---|---|---|
| Shadows | `shadows/directional_cache`, `shadows/skip_*`, `shadows/directional_max_mip`, `shadows/positional_max_mip` | 0004, 0005 |
| Shadows | `shadows/batched_taps` (from `fellow104`: soft shadow filter taps fetched four at a time, stock's arithmetic; frame −2 to −5 %) | 0040 |
| Shadows | `shadows/atlas_simulated_time` (from `fellow160`: at a fixed frame rate — `--fixed-fps`, Movie Maker — the positional shadow atlas waits 500 ms of the scene's time, not of the wall clock, before giving a light a shadow of another size: the same frames on every run) | 0071 |
| Shadows | `shadows/instance_range_cascades` (milestone 147, on: a directional cascade beginning beyond an instance range's end — the terrain's foliage, HLOD's instanced proxies and impostors — leaves those instances out; none of them is drawn that far, so none casts a shadow there; per cascade, so the shadow cache keeps it) | 0272 |
| Geometry | `geometry/mesh_clusters` (frustum culling inside large static surfaces) | 0007 |
| Geometry | `geometry/multimesh_culling` (from `fellow93`: MultiMesh instances culled one by one per pass on the GPU; an autotune tunable), `geometry/multimesh_cull_min_instances`, `geometry/multimesh_cull_min_indices` | 0034 |
| Lights | `lights/cluster_depth_slices`, `lights/zero_attenuation_skip`, `lights/cluster_pack_parallel`, `lights/fields_early` (from `fellow115`, v2 `fellow117`: Bistro interior −3.1 %, Lights Lab −3.0 %), `lights/tile_cull` (from `fellow127`, under autotune: Lights Lab −5.6 %; from `fellow132` split around the async compute part, `debug/tile_cull_split`: Bistro interior −1.9 %), `lights/cluster_pack_elements` and `lights/cluster_instanced_draws` (from `fellow130`: the cluster build without its 21.5 MB clear per frame, GPU −1 to −2.3 % where lights, decals or probes are in view; until milestone 51 debug builds — the editor — skipped its store pass and drew no positional lights) | 0008, 0014, 0045, 0049, 0051 |
| Shading | `shading/blended_alpha_zero_skip`, `shading/absent_features_specialized` (from `fellow142`, 63: the colour passes compiled without the decal, probe, directional, omni, spot and positional shadow code the frame cannot run; GPU −9 % Sponza, −5 to −6 % Bistros and City Lab) | 0010, 0057 |
| Post | `postfx/screen_copy_mipmaps_on_demand` (milestone 24: the screen texture's copy builds its mip chain only when a material drawn this frame samples its mips) | 0123 |
| Post | `postfx/depth_copy_nearest_mipmaps` (milestone 26: a material sampling `hint_depth_texture` with a mipmap filter written out gets the copy with mip levels of the nearest depth of their cells — a Hi-Z pyramid; its base level the same copy; FellowWater3D's reflections traced over it; off: stock's single level) | 0126 |
| Post | `postfx/fast_luminance_reduce`, `postfx/taa_history_pingpong`, `postfx/taa_direct_output`, `postfx/taa_shared_velocity` (start-up), `postfx/ss_last_frame_direct`, `postfx/glow_blur` (from `fellow130`, 2: glow's blur without bank conflicts in 16 × 16 tiles, glow −60 %, GPU −1.1 to −4.5 %), `postfx/tonemap_specialized` (from `fellow138`: tone mapping compiled for the frame's paths only, the pass −12 to −14 %), `postfx/fog_specialized` (from `fellow138`: volumetric fog's density pass without the loops the frame does not run, the pass −21 to −27 %), `postfx/ss_interleave_exact_bounds` and `postfx/ss_depth_mips_exact` (from `fellow158`, correctness: SSAO's last column written at widths such as 1920, where stock leaves it as memory held it; the depth mipmaps without stock's racing stores) | 0025, 0002, 0033, 0044, 0052, 0055, 0056, 0067 |
| Render thread | `cpu/parallel_*`, `cpu/batched_shadow_lists`, `cpu/compact_shadow_instances`, `cpu/persistent_instances`, `cpu/hot_workers_usec` (30 from `fellow156`, was 300: the hot workers no longer spin through a CPU-bound frame; City Lab's 16-45 ms presentation stalls gone, 1 % low 325-358 fps), `cpu/worker_tasks` (automatic from `fellow163`, 6 on the benchmark machine, was 10: City Lab's remaining presentation stalls gone, 1 % low 246-289 fps) | 0016, 0018, 0022, 0065, 0073 |
| Frame | `frame/parallel_replay`, `frame/replay_recorded_segments` (milestone 151, decision 0283, on, diagnostics: off, a list recorded in parallel is replayed split by its draws as any other, not in its recorded segments — tried against decision 0270's rare failure on the RTX, which stayed), `frame/present_thread`, `frame/submit_thread`, `frame/async_compute` (off since milestone 120, decision 0241: on NVIDIA's Windows driver the main queue waited 13-18 ms at the join about once in two to five minutes, for 1-4 % of GPU time in Bistro and Frontier Lab; 2 for NVIDIA GPUs, 1 for every GPU), `frame/pipeline_cache_max_mb` (1024; the pipeline cache no longer crashes at 2 GiB; until milestone 50 the limit was read before the setting existed and was 64 MiB), `frame/pipeline_cache_exclusive` (milestone 136, decision 0261; 2: on Intel GPUs, 1 everywhere, 0 off: the pipeline cache's data read — its size, its copy for the file — only while no pipeline is being created with it; Intel's Windows driver crashed in the periodic save while other threads created pipelines), `debug/segment_barriers` (from `fellow134`: barriers between the command buffers of a frame for resources used again with the same usage), `debug/cloud_shadow_same_frame` (milestone 142, decision 0267, off, diagnostics: the colour passes read the clouds' shadow map written in the same frame, as before decision 0133 — to look again for milestone 33's device loss), `debug/late_fence_report_s` (milestone 146, decision 0270, 2 s, diagnostics: a frame's fence not signalled by then is reported — the frame slots, the present thread's jobs, the transfer workers, the driver's swap chain semaphores, and with `debug/sync_log` the submission that signals it, its waits and where each signal came from — then waited for as before), `debug/sync_log` (milestone 146, decision 0270, off, start-up, diagnostics: the Vulkan driver records its queue operations in order and checks each semaphore wait against the signals before it, each fence wait against the submissions; what could wait for ever is printed when it happens), `debug/gpu_checkpoints` (milestone 146, decision 0270, off, start-up, diagnostics, NVIDIA: a marker the GPU records before each command of the frame; a late fence's report names the last one each queue passed), `debug/graph_reorder` (on) and `debug/graph_full_barriers` (off) (milestone 146, decision 0270, diagnostics: Godot's compile-time switches of the render graph as settings — its commands in their recorded order, or a full barrier between every two), `debug/gate_open_every` (milestone 152, decision 0284, 0, diagnostics: the hot workers' gate open in one frame of every N and closed in the others, whatever the GPU waits, -1 always closed; a late fence's report shows the gate's last 64 frames), `frame/async_ss_split` (from `fellow146`, 3 from `fellow147`: with async compute SSIL runs on the main queue beside the async SSAO, SSR too where autotune measures it faster; Bistro interior −2.7 to −3.8 % GPU), `frame/parallel_shader_create` and `frame/lazy_shader_variants` (from `fellow150`: shaders read from the shader cache created in parallel and only when first asked for; a scene loads to its first frame 12-62 % faster than with stock), `frame/lazy_shader_variants_fill` (milestone 68, on: the variants a cache load left for later made one by one on a worker thread after it, so that a material's variant first used in play — rain's, the rainbow's — is not made on the render thread), `frame/depth_pipeline_keys` (from `fellow158`: the depth passes' pipeline keys without bits their code does not read: fewer duplicate pipelines), `frame/clear_new_textures` (from `fellow160`, correctness: a texture a pass can write, created without data, is cleared before its first use, as stock does on Direct3D 12 — no image depends on what memory held), `frame/movie_wait_pipelines` (from `fellow161`: while Movie Maker mode writes a video, a 3D draw waits for its pipeline instead of drawing with the ubershader — recordings are the same on every run), `frame/stable_scene_bits` (milestone 59, on: the specialization bits that follow what a frame holds — absent decals, probes and lights, the positional lights' loop variants and tile culling, the weather's wetness, the clouds' shadow — keep the feature's code once it exists or has been in a frame: a light switched on or the rain starting no longer recompiles every material in view; the same image), `frame/transfer_tight_staging` (milestone 118, decision 0234, on: a compressed upload's staging its rows of blocks, not of pixels — Sponza streamed's workers 512 → 160-192 MiB), `frame/transfer_staging_trim` (milestone 114; off since milestone 117, decision 0233: a transfer worker unused for `frame/transfer_staging_trim_idle_s`, 5 s, releases its staging buffer above 4 MiB, all but the largest, freed on the render thread (a worker thread until milestone 118) — host memory the driver counts as video memory, 512 MiB in four workers after Sponza's streamed flythrough; a release that empties a 256 MiB block of the memory allocator frees it in about 30 ms, holding up the frame's submission on Windows' NVIDIA driver) | 0015, 0017, 0050, 0054, 0060, 0061, 0069, 0070, 0072, 0161, 0176, 0225, 0233, 0234, 0241, 0261, 0267, 0270, 0283, 0284 |
| Animation | `animation/elide_modifier_backup`, `animation/full_weight_slerp`, `animation/track_shortcuts`, `animation/key_hints` (from `fellow168`, key hints from `fellow170`: skeletal animation on the main thread without work whose result is known — no bone backup around modifiers that change nothing, such as every Skeleton3D's compatibility physical-bone simulator; slerp at weight 1 without its trigonometry; per-track lookups done once; the same bone poses bit for bit; Crowd Lab, 256 animated characters, frame −16 to −20 %), `animation/batched_skin_transforms` (from `fellow170`: a skin's bone transforms handed to the RenderingServer in one call; with the separate render thread Crowd Lab −25 %), `animation/shared_no_override_pose`, `animation/lazy_simulator_poses` (milestone 60: the deprecated global pose without overrides copied where it equals the global pose, the compatibility simulator's pose copies made only when needed; the same bone poses; Crowd Lab's main thread −10 % of its instructions and −7 % of its CPU time, −6 % and −4 % blending); `animation/update_lod` (milestone 140, opt-in: an AnimationPlayer or AnimationTree in the idle or physics process, its root a Node3D, no root motion track, evaluates every frame within `update_lod_distance` (20 m, for a 75° view) of the camera, every 2nd within twice it, every 4th within four times, up to every `update_lod_max_interval`-th (8), every `update_lod_offscreen_interval`-th (8) out of view (`update_lod_offscreen_margin` 2 m), each on its own frames, a skipped frame's time added to its next evaluation; Crowd Lab's 256 characters on a 120 m field: 50 evaluations a frame instead of 256, the main thread's process step 3.4 → 1.2 ms) | 0076, 0077, 0163, 0265 |
| Physics | `physics/distance_face_cull`, `physics/distance_face_order` (from `fellow183`: GodotPhysics3D's shape casts and `move_and_slide()` motion tests skip the trimesh faces a conservative test proves out of reach; ShapeCast3D-heavy lab ×4), `physics/convex_vertex_cache`, `physics/parallel_pair_culls` (broadphase culls on worker threads), `physics/pair_prefilter` (from milestone 111: the existing pairs dropped from those culls on the worker threads), `physics/parallel_island_solve` (a large island solved on every thread in stock's per-body order), `physics/island_constraint_lists`, `physics/jolt_deferred_sensor_contacts`, `physics/jolt_max_jobs` (-1: Jolt's jobs by the work), `physics/motion_query_cache` (a repeated motion test within one `move_and_slide()` answered from the first), `physics/distance_face_shape_axes`, `physics/distance_face_cull_cache`, `physics/distance_face_sweep_reuse` (from `fellow189`: the face test bounds a capsule or sphere by itself, with the faces' edge axes; faces culled once per query; faces out of reach of a sweep not tested again for the sweeps within it), `physics/ray_prune` (raycasts skip trimesh nodes beyond their closest hit); always on: vectorised and inline segment-box tests, `Vector3::is_finite()` inline. The physics state is the same as stock's tick by tick (Physics Lab hashes, any worker count); `physics/verify` checks the skipped faces | 0085-0091, 0093, 0094, 0096-0100, 0103, 0219 |
| Autotune | `autotune/enabled` (chooses the scene-dependent exact options by measurement), `autotune/continue_undecided` and `autotune/skip_creation_frames` (from `fellow153`: an undecided evaluation of an option that starts off is continued next when it leans on; frames that compile pipelines are not counted), `autotune/prepare_pipelines` (from `fellow158`: an evaluation that switches a pipeline bit waits until the pipelines of the materials in view with the other value are compiled, instead of drawing with the ubershader in its first frames) | 0020, 0064, 0068 |

`postfx/taa_resolve_v2` (on) is a look change from milestone 1 (TAA with reverse-Z dilation and YCoCg variance
clipping, decision 0001). From milestone 189 (decision 0331) it keeps its history where it is trusted (inside the
neighbourhood's colour box) the more the jittered frame disagrees with it, by the tonemapped lumas' difference as stock's
resolve does — `postfx/taa_v2_stability` (1.0; 0: milestone 188's resolve), `postfx/taa_v2_stability_contrast` (1.0:
stock's measure; more, steadier and further from the truth) — and softens a moving pixel's history towards the frame's
3x3 mean only where the history was clipped, `postfx/taa_v2_soften_untrusted` (1.0; 0: everywhere, as before): the
jitter's flicker on detail finer than a pixel (shutters' slats, rough stone, joints) averaged instead of followed. The
rooftops on the RTX: the buildings' pixels flickering by more than 3 levels 6.7-7.6 → 1.8-1.9 % with the camera still
and 8.0-9.6 → 1.6-1.9 % (motion-compensated) moving, as steady as stock's TAA still and steadier moving, sharper than
both moving; PBR Lab against its ground truth within 0.1 dB of milestone 188 where the camera pans, 0.2-1.5 dB further
elsewhere (most where foliage is strafed).

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
