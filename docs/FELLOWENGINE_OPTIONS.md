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
| `fire_intensity` (20), `fire_temperature_scale` (1.4) | the fire's blackbody emission (brightness by the fourth power of the temperature) and its colour's temperature |
| `cast_shadows`, `shadow_strength` | the sun's shadow through the smoke on the scene (in the contact shadow mask; not with the ray-traced sun shadows) |
| `fire_light`, `fire_light_energy`, `fire_light_range`, `fire_light_shadows` | the omni light at the fire, from the simulated emission (2-3 frames late) |
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
  no glints, its thin parts first; a FellowWeather3D sets it while the snow melts.

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
`FellowLightning3D` (found, or created when needed: `create_missing`; all at the start: `prepare_nodes`); the
volumetric clouds' coverage and density in place of the project settings (the clouds must be on), and so their
shadows (decision 0133); the sun on the scene's surfaces only where the clouds cast no shadows (its light buffer
energy times `sunlight` — the sky and the clouds keep the full sun); the
environment's fog (added to its own, in `fog_color` as bright as the sky) and its background energy (the sky, its
ambient light and reflections); and the ground — wet in the rain, drying after it, covered by falling snow below 1 °C,
melting above 0 °C, its water freezing and frosted below 0 °C when nothing falls (decision 0136) (`ground_speed`
speeds it up; `settle_ground` starts it as the weather leaves it). It does nothing
in the editor. Without the node nothing changes (bit for bit); with it, the cost is that of the parts it turns on.
Decision 0132.

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
| Lights | `lights/cluster_depth_slices`, `lights/zero_attenuation_skip`, `lights/cluster_pack_parallel`, `lights/fields_early` (from `fellow115`, v2 `fellow117`: Bistro interior −3.1 %, Lights Lab −3.0 %), `lights/tile_cull` (from `fellow127`, under autotune: Lights Lab −5.6 %; from `fellow132` split around the async compute part, `debug/tile_cull_split`: Bistro interior −1.9 %), `lights/cluster_pack_elements` and `lights/cluster_instanced_draws` (from `fellow130`: the cluster build without its 21.5 MB clear per frame, GPU −1 to −2.3 % where lights, decals or probes are in view) | 0008, 0014, 0045, 0049, 0051 |
| Shading | `shading/blended_alpha_zero_skip`, `shading/absent_features_specialized` (from `fellow142`, 63: the colour passes compiled without the decal, probe, directional, omni, spot and positional shadow code the frame cannot run; GPU −9 % Sponza, −5 to −6 % Bistros and City Lab) | 0010, 0057 |
| Post | `postfx/screen_copy_mipmaps_on_demand` (milestone 24: the screen texture's copy builds its mip chain only when a material drawn this frame samples its mips) | 0123 |
| Post | `postfx/depth_copy_nearest_mipmaps` (milestone 26: a material sampling `hint_depth_texture` with a mipmap filter written out gets the copy with mip levels of the nearest depth of their cells — a Hi-Z pyramid; its base level the same copy; FellowWater3D's reflections traced over it; off: stock's single level) | 0126 |
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
