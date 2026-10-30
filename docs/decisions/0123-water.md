# 0123 — Water bodies: FFT wind waves, the simulated river blended in, reflections, refraction, foam, caustics

Status: **KEEP** (a new node; without it nothing of it runs) · Branch `fellow/geometry` · Node `FellowWater3D` (module
`fellow_fluid`) · Code: `modules/fellow_fluid/fellow_water_3d.{h,cpp}` (the node, the spectrum, the render thread),
`fellow_water_waves.glsl` (evolution, FFT, assembly, mip levels, caustics), `fellow_water_shader.h` (the surface's
material), `fellow_swe.glsl` (the shallow-water surface kernel also writes the flow); the screen copy's mip levels on
demand in `renderer_scene_render_rd.cpp` and `render_forward_clustered.cpp` (`postfx/screen_copy_mipmaps_on_demand`);
check `benchmarks/project/bench/water_check.gd` + `modules/fellow_fluid/tests/waves_compare.py`; bench scene Water
Lab (`--scene=water`).

## Problem

The user asked (2026-10-28) for the look of water: lakes and rivers with reflections, refraction, foam at the shores,
caustics, built on the GPU shallow-water simulation (0003). That simulation's surface was milestone 1's prototype:
a 64 m tile, flat green-teal with no fine waves (the surface is only as detailed as its grid), the sky's IBL as its
only reflection, ending at the tile's edge. A lake larger than a simulation tile had nothing.

## Change

`FellowWater3D` is the body of water: a surface at the node's height, to the horizon or within `extent`.

**Wind waves** (tier 1 of `docs/fellow_fluid_architecture.md`, Tessendorf). A JONSWAP spectrum for the wind speed and
fetch (a small lake's short waves to the sea's long ones) with the Donelan-Banner directional spreading, in up to four
cascades (tiles of 160, 27 and 5 m by default, N x N each, N = 256): a cascade holds the wave numbers from six of its
own fundamentals to where the next, shorter one starts. h0(k) is built on the CPU (deterministic: a hash of the
texel, Box-Muller), the Nyquist row and column left empty (they have no mirror on the grid: every field stays real,
which the packing below relies on). Per frame on the GPU (`fellow_water_waves.glsl`):

1. **evolve**: h(k, t) = h0(k) e^{iωt} + conj(h0(−k)) e^{−iωt}, ω quantized to multiples of 2π / `repeat_period`
   (200 s; the phase is 2π fract(j · t / T): exact however long the clock runs); the eight fields — displacement x,
   y, z (choppy: D = i k/|k| h), the height's slopes, the horizontal displacement's derivatives — packed two per
   complex spectrum into two RGBA32F images;
2. **FFT**: inverse, rows then columns, Stockham radix 2 in shared memory (one row per group, two complex numbers per
   element; 16 KB of shared memory at N = 512);
3. **assemble**: displacement (x, y, z, foam) and slopes (sx, sz, sx², sz²) per cascade, RGBA16F; foam accumulates
   where the surface folds (the displacement's Jacobian below `whitecap_threshold`) and decays;
4. **mip levels** of both (2 x 2 means: the slope variance survives as E[s²] − E[s]²) and of
5. **caustics**: per texel the sun's ray refracted by the slope reaches a plane 0.6 m and 2.5 m below at p + o(p); the
   light per area there is 1 / det(∂(p + o)/∂p) (finite differences, clamped to 5).

Verified against NumPy (`water_check.gd` + `waves_compare.py`: h0 as uploaded, evolved and transformed with
numpy.fft): the three cascades within 2 x 10⁻⁴ m of up to 0.28 m, half-float precision. The spectrum's significant
wave height at 8 m/s and 5 km of fetch: 0.34 m (the fetch-limited JONSWAP relation gives 0.29 m).

**The surface mesh** is a polar grid around the camera: rings growing by 1 + 2π/segments (cells stay square; 256
segments, 0.25 m to 20 km: 120 k vertices), placed at the camera in the vertex shader (`CAMERA_POSITION_WORLD`), its
vertex spacing in UV.x. The displacement is sampled at a mip level matching the spacing (band-limited: it neither
aliases nor swims as the grid moves with the camera); the slopes per pixel.

**The simulation** (`simulation_path` → a `FellowShallowWater3D`): inside its domain the surface is the simulated one
(its height; its slopes added to the waves'; its foam), blended into the body's over `simulation_blend` (4 m) from
its edges; the simulation's own mesh is hidden. Its surface kernel now also writes the flow (cell-centre velocity and
depth): the waves are **carried by the flow** (flow mapping, two phases half of `flow_period` apart), scaled down
inside it (`simulation_wave_scale`) and faded out in water shallower than 60 cm. Dry cells take the level of a wet
neighbour (the terrain then cuts the waterline smoothly; at the cells' edges it was a staircase), else sink a metre.

**Shading** (one transparent material, `fellow_water_shader.h`):

* **Reflections** traced in screen space against the opaque scene's depth (32 quadratically spaced steps up to
  `reflection_distance`, refined by bisection), faded at the screen's edges. Where nothing near is hit, the screen's
  view in the ray's direction when it shows something distant (the far shore, the sky); else the sky's IBL (Godot's,
  through SPECULAR, which here only scales it). A ray a tilted facet sends below the horizon is turned back up (it
  would leave by the next wave): without that the far water reflected the IBL's ground (dark).
* **Fresnel** (F0 0.02) between the reflection and what lies below.
* **Refraction**: the scene below through the screen texture, shifted by the normal by the water's thickness over
  the distance; a shifted sample in front of the surface falls back to the unshifted one.
* **Absorption** along the light's path down to the bottom and the view's path up from it (`absorption` per metre:
  pure water's red, dissolved matter's blue — a temperate lake's green); **in-scattering** as an albedo lit by the
  scene's lights (`scattering_color`): the water's body receives shadows.
* **Caustics** on the refracted bottom: the bottom point traced back to the surface along the refracted sun, the
  cascades of at most 12 m sampled there (carried by the flow too), interpolated between the two depths, fading with
  depth and with the sun's height.
* **Foam**: breaking waves (the cascades' foam), shores and objects in the water (`shore_foam_depth`), the
  simulation's; a tileable cellular noise thresholded by the amount.
* **The sun's highlight** in `light()` (GGX with water's F0, Smith-Schlick; off on foam), its roughness from
  `roughness` and the slope variance the pixel does not resolve (α² = α₀² + σ²): a far surface reflects a glitter
  path, not a mirror image of the sun.
* `debug_view`: the reflection weight and Fresnel, foam, refraction, normals, the simulation's weight, roughness.

**The screen copy's mip levels on demand** (`rendering/fellow/postfx/screen_copy_mipmaps_on_demand`, on): stock copies
the screen texture for `hint_screen_texture` and always builds its whole mip chain (0003 measured the copy as the
water's largest cost). The scene shader already records whether a material samples the mips (a `filter_*_mipmap`
hint); the chain is now built only when a material drawn this frame does. The water reads the base level only.

## Results

**Exactness.** Without a `FellowWater3D` nothing of it runs; the shallow-water node's surface kernel writes a second
texture (the flow) but its own image is unchanged: Fluid Lab bit for bit the images of the build before (`6f091cd`).
The screen copy's mips on demand: Water Lab (FellowWater3D, five cameras) and Water Lab with Godot's water (a
StandardMaterial3D with refraction, which samples the mips) bit for bit with the option on and off and against the
build before; Fluid Lab (its material samples the mips) the same.

**Look** (Water Lab, lavapipe 960 x 540; `--water=basic` is what a Godot project would use: the milestone-1 surface on
the river, a StandardMaterial3D plane with refraction on the lake). Godot's lake is a grey-blue mirror of the sky,
the pier's posts reflected as if in glass, no waves, the sun a single dot; FellowWater3D: the far shore and its trees
mirrored and broken up by the waves, green shallows over the sand with caustics, the bottom fading into the deep
water, the pier's posts and the boat reflected and their shadows in the water's body, a glitter path towards the sun,
the river's flow and wakes running into the lake without a seam.

**Cost on lavapipe** (480 x 270, mean of two runs, GPU time of the viewport; the waves' compute and the simulation run
outside it and show in the frame time):

| camera | no water | Godot's water | FellowWater3D |
|---|---:|---:|---:|
| flythrough | 346.1 ms | 404.8 ms | 481.4 ms |
| shore | 306.6 ms | 326.5 ms | 428.0 ms |
| pier | 264.1 ms | 278.7 ms | 427.6 ms |
| river | 337.8 ms | 379.2 ms | 435.7 ms |
| aerial | 293.8 ms | 351.6 ms | 375.8 ms |
| glint | 312.9 ms | 382.5 ms | 462.5 ms |

The waves' compute is about 70 ms of lavapipe's time per frame (three cascades of 256²; a software rasterizer's
compute is slow), frame time 595 ms against 452 ms with Godot's water in the flythrough. The pier's view, all water
near the camera, costs most (the reflection rays march their 32 steps where nothing is hit). The RTX measurement
decides.

## RTX 5070 Ti Laptop (`m24_f269_water`, fellow202, 1080p, autotune off, 3 runs each, pass timestamps)

| Water Lab | frame (flythrough) | GPU | shore | pier | glint | aerial | river |
|---|---:|---:|---:|---:|---:|---:|---:|
| no water | 1.615 ms | 1.547 ms | 1.485 | 1.601 | 1.561 | 1.676 | 1.514 |
| Godot's water | 1.999 ms | 1.777 ms | 1.684 | 1.921 | 1.829 | 1.872 | 1.719 |
| FellowWater3D | 3.482 ms | 3.148 ms | 2.401 | 3.098 | 3.247 | 2.108 | 1.648 |
| without its reflections | 2.427 ms | 2.091 ms | 1.837 | 2.303 | 2.066 | 1.914 | 1.628 |
| without its caustics | 3.456 ms | 3.123 ms | 2.355 | 3.037 | 3.246 | 2.138 | 1.640 |
| screen copy's mips always | 3.469 ms | 3.140 ms | 2.462 | 3.191 | 3.305 | 2.090 | 1.649 |

(per camera: GPU ms.) The passes (FellowWater3D): the transparent pass, where the water is drawn, 1.591 ms (Godot's
water 0.166 ms; without the reflections 0.459 ms) — **the reflections' march is 1.1 ms**; the waves' compute 0.101 ms
(three cascades of 256², FFT, assembly, 24 mip levels, caustics); the simulation 0.14 ms (as with Godot's water); the
caustics 0.03 ms; the screen copy 0.031 ms with its mips on demand, 0.064 ms with them always (the frame within the
runs' spread). The river's camera, mostly banks, is cheaper with FellowWater3D than with Godot's water (its plane and
the copy's mips go).

Captures at 1080p (`m24_f269_water_capture`): as on lavapipe — the far shore and its trees mirrored and broken by the
waves, the pier's posts and the boat reflected, green shallows with caustics, a glitter path; Godot's water a grey-blue
mirror without waves, the sun a single dot.

**The cheaper march** (after f269): no march where Fresnel is below `reflection_min_fresnel` (0.04: looking down into
the water, the sky's IBL alone), the steps by the ray's length on the screen (one per 10 pixels, 6 to
`reflection_steps`), the samples offset per pixel and frame (interleaved gradient noise; TAA averages). `m24_f270_water`
(fellow203, the same settings):

| | flythrough frame | transparent pass | shore | pier | glint | aerial | river |
|---|---:|---:|---:|---:|---:|---:|---:|
| no water | 1.657 ms | — | 1.522 | 1.626 | 1.636 | 1.751 | 1.541 |
| FellowWater3D | 3.264 ms (was 3.482) | 1.389 ms (was 1.591) | 2.306 | 2.956 | 3.102 | 2.085 | 1.616 |
| without its reflections | 2.372 ms | 0.463 ms | 1.779 | 2.272 | 2.041 | 1.894 | 1.611 |

The reflections 0.93 ms instead of 1.13 (−18 %); captures against f269's 45.0 / 49.3 / 54.7 / 54.8 / 56.6 dB (the
differences in the reflections of the pier and the far shore: the jittered samples). Most of the march's cost is in
the grazing views (the glint's 1.06 ms, the pier's far half): rays towards the horizon cross the screen and need their
steps. FellowWater3D costs 1.6 ms of a 1080p frame on this GPU (Godot's water 0.38 ms), the reflections more than half
of it; `screen_space_reflections` off or fewer `reflection_steps` trade them away.

**The hierarchical march** (milestone 26, decision 0126): with the depth texture's nearest-depth mip levels
(`postfx/depth_copy_nearest_mipmaps`, on) the rays cross the screen a cell of a level per step — no gaps, no jitter,
the far shore at its true parallax; at 1080p 12-31 % fewer depth lookups in the shore, pier and glint views, 23 % more
in the aerial one. The linear march above remains where the depth texture has one level.

**Where the rest goes** (`m26_f278_water`, fellow210, 1080p, 3 runs each, GPU time against Water Lab without water,
1.611 ms): FellowWater3D 1.14 ms — the water's pass 1.069 ms (the reflections 0.61; without them 0.455 ms, of it the
caustics 0.019 and the refraction 0.012 ms: 0.42 ms are the cascades' slopes and foam, the foam's noise and the
lighting; measurement builds `m26_f281`: without the cascades' lookups the pass is 0.211 ms cheaper — 0.098 of it
their anisotropic filtering —, without the foam's noise 0.013 ms), the river's simulation 0.16 ms, the waves' compute 0.11 ms, the depth copy with its pyramid 0.08 ms (0.026
without reflections, since `560d36c`), the screen copy 0.03 ms.

## Decision

**KEEP** — a new node, nothing changes without it. The screen copy's mips on demand: exact, on by default (the copy
0.064 → 0.031 ms in Water Lab).
