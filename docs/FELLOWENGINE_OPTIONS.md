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
Runs only in a running game; it replaces the mesh instance's mesh and materials while in the tree. Decision record 0120; bench scene Burn Lab (`--scene=burn`).

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
  views of a mesh takes about 70 frames at start. Measure with your own meshes before using them.

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
environment's fog (added to its own, in `fog_color` as bright as the sky) and its background energy (the sky, its
ambient light and reflections); and the ground — wet in the rain, drying after it, covered by falling snow below 1 °C,
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
releases them when they no longer do. The level each texture needs comes from the instances that use it (their box's
nearest view depth, their smallest scale, their surfaces' texel density, BaseMaterial3D's `uv1_scale`) and the
samplers' bias; `texture_lod_bias` (-1) is the margin, `texture_budget_mb` (0: none) caps the streamed textures'
memory (every texture coarsened by the same bias over it). `texture_instances_per_frame` (2048) instances are
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
plus `unload_margin`), `max_mesh_loads` 2 at once. The editor runs it from Project > Tools > **Bake Streaming
Proxies**. Bench `bench/world_proxies_check.gd`.

## FSR 3.1 upscaler (milestone 47)

`rendering/fellow/upscaling/fsr3` (on from milestone 49; read every frame): a viewport in Godot's FSR 2 scaling mode
(`scaling_3d_mode` FSR 2, any `scaling_3d_scale`, 1.0 for native AA) runs AMD's FidelityFX Super Resolution 3.1
upscaler instead of FSR 2.2, with the same inputs, jitter, sharpness (`fsr_sharpness`) and output; switching makes a
new context (a new history). `fsr3_half_precision` (on; read at start-up): half precision in the passes that have it,
where the GPU supports it (not SPD's pyramids, not the accumulation on NVIDIA). The SDK's tunables:
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
`--many=N`).

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
FellowEngine menu's "Add Destructible Crate". Bench `bench/destruction_check.gd` (`--many=N`, `--prefracture=1`); the
regression scene `destruction`.

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
`bone_pins`.

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
frame). Decision 0151; check `benchmarks/tools/editor_check/run_editor_check.py`.

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
Impostors button (next to the scene, `impostors/`, undoable). Bench `bench/terrain_holes_check.gd`.

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

## Options that change the image (off by default)

| setting | what it does | image cost | record |
|---|---|---|---|
| `geometry/multimesh_instance_lod` (with `geometry/multimesh_culling`) | each MultiMesh instance gets the mesh LOD level stock gives a node of its own (distance to its box, the project's `mesh_lod_threshold`), instead of the level of the MultiMesh's whole box | not visually lossless where many far instances coarsen together: Foliage Lab from above 33-40 dB (far grass clumps lose blades); at ground level 48.2-64.6 dB; −2.2 % with the bench's light LOD meshes | 0035 |
| `geometry/multimesh_cull_vertex_motion` (with `geometry/multimesh_culling`) | MultiMeshes whose material moves vertices (wind) are culled too when their node has an extra cull margin: the margin is taken as how far the vertices move | none while the material keeps within the node's margin (Foliage Lab with wind: 0 differing pixels); beyond it, instances near the view's edges can be lost | 0036 |
| `geometry/contribution_cull_pixels` (0 = off) | contribution culling: the camera does not draw a mesh or MultiMesh node whose bounding sphere would cover fewer than this many pixels of the viewport's width (as `mesh_lod_threshold`), nor, with `geometry/multimesh_culling`, such a MultiMesh instance; nodes with `ignore_occlusion_culling` are always drawn | tiny far objects disappear: City Lab at 2 px of 960 (about 4 px at 1080p) 62.9-78.4 dB, 292 pixels over three captures; 5-14 % of the city's nodes not drawn, 10-13 % fewer primitives | 0121 |
| `geometry/contribution_cull_instances` (Thin) | MultiMesh instances below the threshold: thinned (a share kept and grown to the threshold's size, so a far meadow keeps its coverage) or culled | Foliage Lab aerial at 2 px of 960: thinned 41.1 dB with the meadow's mean colour unchanged; culled 36.8 dB, the meadow 3.7 levels lighter | 0121 |
| `shadows/contribution_cull_texels` (0 = off) | a caster whose box's diagonal spans fewer than this many texels of a directional cascade is not drawn into it (per node; per instance for culled MultiMeshes) | the shadows of tiny far casters disappear: City Lab at 2 texels 0 differing pixels (its smallest casters span 6 texels of the far cascade) | 0121 |
| `geometry/cluster_lod` (with `geometry/mesh_clusters`) | each cluster of a large static surface is drawn at the mesh LOD stock would give it as an instance of its own (the main camera's distance to the cluster), with its neighbours' finer levels where they meet (no cracks) | the image as LOD changes it: Sponza 63.6 dB on a moving sequence, stills 52.9-56.8 dB at distant silhouettes; Sponza GPU −3.1 %, render thread +0.21 ms | 0063 |

## Exact options (on by default unless noted)

| area | settings | record |
|---|---|---|
| Shadows | `shadows/directional_cache`, `shadows/skip_*`, `shadows/directional_max_mip`, `shadows/positional_max_mip` | 0004, 0005 |
| Shadows | `shadows/batched_taps` (from `fellow104`: soft shadow filter taps fetched four at a time, stock's arithmetic; frame −2 to −5 %) | 0040 |
| Shadows | `shadows/atlas_simulated_time` (from `fellow160`: at a fixed frame rate — `--fixed-fps`, Movie Maker — the positional shadow atlas waits 500 ms of the scene's time, not of the wall clock, before giving a light a shadow of another size: the same frames on every run) | 0071 |
| Geometry | `geometry/mesh_clusters` (frustum culling inside large static surfaces) | 0007 |
| Geometry | `geometry/multimesh_culling` (from `fellow93`: MultiMesh instances culled one by one per pass on the GPU; an autotune tunable), `geometry/multimesh_cull_min_instances`, `geometry/multimesh_cull_min_indices` | 0034 |
| Lights | `lights/cluster_depth_slices`, `lights/zero_attenuation_skip`, `lights/cluster_pack_parallel`, `lights/fields_early` (from `fellow115`, v2 `fellow117`: Bistro interior −3.1 %, Lights Lab −3.0 %), `lights/tile_cull` (from `fellow127`, under autotune: Lights Lab −5.6 %; from `fellow132` split around the async compute part, `debug/tile_cull_split`: Bistro interior −1.9 %), `lights/cluster_pack_elements` and `lights/cluster_instanced_draws` (from `fellow130`: the cluster build without its 21.5 MB clear per frame, GPU −1 to −2.3 % where lights, decals or probes are in view; until milestone 51 debug builds — the editor — skipped its store pass and drew no positional lights) | 0008, 0014, 0045, 0049, 0051 |
| Shading | `shading/blended_alpha_zero_skip`, `shading/absent_features_specialized` (from `fellow142`, 63: the colour passes compiled without the decal, probe, directional, omni, spot and positional shadow code the frame cannot run; GPU −9 % Sponza, −5 to −6 % Bistros and City Lab) | 0010, 0057 |
| Post | `postfx/screen_copy_mipmaps_on_demand` (milestone 24: the screen texture's copy builds its mip chain only when a material drawn this frame samples its mips) | 0123 |
| Post | `postfx/depth_copy_nearest_mipmaps` (milestone 26: a material sampling `hint_depth_texture` with a mipmap filter written out gets the copy with mip levels of the nearest depth of their cells — a Hi-Z pyramid; its base level the same copy; FellowWater3D's reflections traced over it; off: stock's single level) | 0126 |
| Post | `postfx/fast_luminance_reduce`, `postfx/taa_history_pingpong`, `postfx/taa_direct_output`, `postfx/taa_shared_velocity` (start-up), `postfx/ss_last_frame_direct`, `postfx/glow_blur` (from `fellow130`, 2: glow's blur without bank conflicts in 16 × 16 tiles, glow −60 %, GPU −1.1 to −4.5 %), `postfx/tonemap_specialized` (from `fellow138`: tone mapping compiled for the frame's paths only, the pass −12 to −14 %), `postfx/fog_specialized` (from `fellow138`: volumetric fog's density pass without the loops the frame does not run, the pass −21 to −27 %), `postfx/ss_interleave_exact_bounds` and `postfx/ss_depth_mips_exact` (from `fellow158`, correctness: SSAO's last column written at widths such as 1920, where stock leaves it as memory held it; the depth mipmaps without stock's racing stores) | 0025, 0002, 0033, 0044, 0052, 0055, 0056, 0067 |
| Render thread | `cpu/parallel_*`, `cpu/batched_shadow_lists`, `cpu/compact_shadow_instances`, `cpu/persistent_instances`, `cpu/hot_workers_usec` (30 from `fellow156`, was 300: the hot workers no longer spin through a CPU-bound frame; City Lab's 16-45 ms presentation stalls gone, 1 % low 325-358 fps), `cpu/worker_tasks` (automatic from `fellow163`, 6 on the benchmark machine, was 10: City Lab's remaining presentation stalls gone, 1 % low 246-289 fps) | 0016, 0018, 0022, 0065, 0073 |
| Frame | `frame/parallel_replay`, `frame/present_thread`, `frame/submit_thread`, `frame/async_compute` (NVIDIA), `frame/pipeline_cache_max_mb` (1024; the pipeline cache no longer crashes at 2 GiB; until milestone 50 the limit was read before the setting existed and was 64 MiB), `debug/segment_barriers` (from `fellow134`: barriers between the command buffers of a frame for resources used again with the same usage), `frame/async_ss_split` (from `fellow146`, 3 from `fellow147`: with async compute SSIL runs on the main queue beside the async SSAO, SSR too where autotune measures it faster; Bistro interior −2.7 to −3.8 % GPU), `frame/parallel_shader_create` and `frame/lazy_shader_variants` (from `fellow150`: shaders read from the shader cache created in parallel and only when first asked for; a scene loads to its first frame 12-62 % faster than with stock), `frame/lazy_shader_variants_fill` (milestone 68, on: the variants a cache load left for later made one by one on a worker thread after it, so that a material's variant first used in play — rain's, the rainbow's — is not made on the render thread), `frame/depth_pipeline_keys` (from `fellow158`: the depth passes' pipeline keys without bits their code does not read: fewer duplicate pipelines), `frame/clear_new_textures` (from `fellow160`, correctness: a texture a pass can write, created without data, is cleared before its first use, as stock does on Direct3D 12 — no image depends on what memory held), `frame/movie_wait_pipelines` (from `fellow161`: while Movie Maker mode writes a video, a 3D draw waits for its pipeline instead of drawing with the ubershader — recordings are the same on every run), `frame/stable_scene_bits` (milestone 59, on: the specialization bits that follow what a frame holds — absent decals, probes and lights, the positional lights' loop variants and tile culling, the weather's wetness, the clouds' shadow — keep the feature's code once it exists or has been in a frame: a light switched on or the rain starting no longer recompiles every material in view; the same image) | 0015, 0017, 0050, 0054, 0060, 0061, 0069, 0070, 0072, 0161, 0176 |
| Animation | `animation/elide_modifier_backup`, `animation/full_weight_slerp`, `animation/track_shortcuts`, `animation/key_hints` (from `fellow168`, key hints from `fellow170`: skeletal animation on the main thread without work whose result is known — no bone backup around modifiers that change nothing, such as every Skeleton3D's compatibility physical-bone simulator; slerp at weight 1 without its trigonometry; per-track lookups done once; the same bone poses bit for bit; Crowd Lab, 256 animated characters, frame −16 to −20 %), `animation/batched_skin_transforms` (from `fellow170`: a skin's bone transforms handed to the RenderingServer in one call; with the separate render thread Crowd Lab −25 %), `animation/shared_no_override_pose`, `animation/lazy_simulator_poses` (milestone 60: the deprecated global pose without overrides copied where it equals the global pose, the compatibility simulator's pose copies made only when needed; the same bone poses; Crowd Lab's main thread −10 % of its instructions and −7 % of its CPU time, −6 % and −4 % blending) | 0076, 0077, 0163 |
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
