# 0008 — Light clusters with depth slices

Status: **KEEP** · Branch `fellow/geometry` · Toggles
`rendering/fellow/lights/cluster_depth_slices`, `rendering/fellow/lights/zero_attenuation_skip` (default **on** from
`fellow22`) · Code: `cluster_builder_rd.h/.cpp`
(`_fellow_store_depth_range`, `begin`), `shaders/cluster_store.glsl`, `shaders/forward_clustered/scene_forward_clustered_inc.glsl`
(`fellow_cluster_z`), `scene_forward_clustered.glsl`, `shaders/environment/volumetric_fog_process.glsl`, `environment/fog.*`,
`shaders/cluster_debug.glsl`, scene UBO flag 14.

## Problem

Forward+ finds the lights, decals and reflection probes of a pixel through clusters: 32 × 32-pixel screen tiles, each
with a bit mask of the elements whose proxy mesh covers the tile, and 32 depth slices per tile that narrow the list to
the elements present at the pixel's depth. Stock spaces the 32 slices **linearly over [0, camera far]**
(`scene_forward_clustered.glsl`: `cluster_z = -vertex.z / z_far * 32`). Godot's default far plane is 4000 m, so every
slice is 125 m deep: in practice every pixel loops over every element whose proxy covers its tile anywhere in depth.
In a street, a light 5 m in front of the camera covers a large part of the screen, and every pixel behind it — 50 or
200 m away — iterates it (loading its data, computing its attenuation, finding it zero).

## Evidence (before the change)

Offline count from the engine's own geometry and light data (`--mode=geodump`, `benchmarks/tools/light_cluster_geodump.py`:
stock's tile test and slices modelled with the lights' bounding spheres; depth of every pixel from the opaque
geometry; mean over the flythrough, lights iterated per shaded pixel):

| scene | lights | stock clusters | depth slices | lights that reach the pixel |
|---|---:|---:|---:|---:|
| PBR Lab | 2 | 1.74 | 1.56 | 1.19 |
| Lights Lab (new, 242 lights in a 240 m street) | 242 | 33.9 | 19.1 (−44 %) | 11.5 |
| Bistro interior | 4 (shadowed omni, 15 m range) | 4.0 | 4.0 | 4.0 (every light reaches every pixel) |
| Bistro exterior | 0 (sun only) | – | – | – |

The benchmark's Bistro scenes have almost no positional lights (the imported exterior has none; the interior four that
light the whole room), so the change can only show in the Lights Lab there; Sponza has none either.

## Change

1. **Slices logarithmic in view depth** between the near and far planes: `slice = floor(log2(z / near) · 32 /
   log2(far / near))` (near 0.05 m, far 4000 m: each slice spans a factor 1.42 in depth, e.g. 7-10 m, 10-14 m). The
   same function in the scene shader (pixels, vertex lighting), the volumetric fog (froxels) and the cluster debug view.
2. **Every element covers the slices of its own conservative view-depth range**, computed on the CPU when it is added:
   omni lights and wide spots: centre ± range (overfit radius, and never less than the unscaled range the shader
   attenuates with); narrow spots: the depth extent of the proxy cone (apex and base disc), which contains the lit
   region; decals, reflection probes and area lights: the depth extent of their box (sum of the axes' depth
   components — stock's `box_depth` projects the extents with signs and can be smaller than the box). The range is
   widened by 0.1 % so that the GPU's evaluation of the slice function can never put a depth inside it into a slice
   outside it. It replaces stock's per-tile range, which is *sampled* from the proxy mesh's rasterisation.
3. Perspective cameras with a single view only (multiview builds one cluster for both eyes: stock); orthographic
   cameras: stock.

The tile masks, the element order (by distance) and the shading are unchanged; only which elements a pixel visits.

## Why it is exact

An element can only affect a pixel whose surface point is inside its volume (zero attenuation outside the range or the
spot cone, decals and probes test their box). A point inside the volume has a view depth inside the element's depth
range, so its slice is one the element covers: the pixel still visits it. The elements a pixel no longer visits
contribute exactly zero (`light_compute` skips zero attenuation; the shadow lookups are guarded by it).

## Verification

* lavapipe, Lights Lab (194 omni, 48 spot of which 22 shadowed, 16 decals, 4 reflection probes, volumetric fog, a
  scaled light, the camera inside a light volume), 4 fixed cameras after 30 settle frames, stock clusters vs depth
  slices: 3 cameras **0 differing pixels**; one camera 2 pixels by 1/255 (fog under TAA).
  The cause is stock: with every element forced into **all 32 slices** (a superset of stock's lists, both with stock's
  slice function and with the new one) the same 2 pixels change by the same amount, i.e. stock's sampled per-tile
  ranges drop an element that reaches a fog froxel; the depth-sliced result is **bit-identical** to that complete set.
  (A first version intersected the new ranges with stock's sampled ones and could therefore inherit such misses; it
  also used stock's signed `box_depth` for decals, which cut a decal: 3187 pixels. Both fixed before any measurement.)
* lavapipe, PBR Lab (7 cameras) and Stress Lab (4 cameras: omni and spot shadows, a reflection probe updated every
  frame, a second viewport): **0 differing pixels**.
* RTX 5070 Ti (`fellow17`, `m3_lc_temporal`, 60-frame sequences, default configuration vs + depth slices): Lights Lab
  96.8 dB mean (min 79.5, max |d| 3/255), Bistro exterior 100.6 dB (max 1), Bistro interior 66.0-67.3 dB and PBR Lab
  65.6-76.1 dB (≤ 0.004 % of pixels > 2/255: those scenes' own run-to-run floor, as in the null tests).
* RTX 5070 Ti fixed cameras (`m3_lc_capture`, 1920×1080): Lights Lab **bit-identical** (4 cameras, max |d| 0),
  Bistro exterior 76.8-99 dB (max 2/255), Bistro interior, PBR Lab, Sponza at their run-to-run floor (68-71 dB min,
  ≤ 0.002 % of pixels > 2/255).
* RTX timing (`fellow18`, `m3_lc_perf`: 1920×1080, day state, 3 runs per configuration and scene, 6 for the Lights
  Lab; the default configuration vs + depth slices, sequential, nothing else running):

  | scene | frame ms (default → depth slices) | Δ | p (Welch) | verdict |
  |---|---:|---:|---:|---|
  | Lights Lab | 3.379 → 3.111 | **−0.268 (−7.9 %)** | 0.002 | faster (the reference's CV 3.3 %) |
  | Bistro interior | 4.276 → 4.311 | +0.035 (+0.8 %) | 0.24 | within noise |
  | Bistro exterior | 5.060 → 5.050 | −0.010 | 0.57 | within noise |
  | Sponza | 8.238 → 8.265 | +0.027 | 0.62 | within noise |
  | PBR Lab | 2.831 → 2.812 | −0.019 | 0.41 | within noise |

  Profile (`m3_lc_profile`, one run each): Lights Lab opaque pass 1.495 → 1.268 ms (−0.227), cluster packing
  0.079 → 0.118 ms (+0.039: an element near the camera now covers several slices, each one a store). Bistro interior's
  opaque pass read +0.13 ms in that single profiled run; the 3-run A/B above puts the frame at +0.035 (p 0.24), and
  `fellow19`'s profile repeats the comparison.
  (A first attempt overlapped other sessions and was discarded.)

## Zero-attenuation skip (`lights/zero_attenuation_skip`, `fellow19`)

With depth slices a pixel still visits elements whose sphere range contains its depth but not its position (19 visited
vs 11.5 reaching per pixel in the Lights Lab). For those, stock computes the attenuation (a `pow()`), the light size,
the N·L skip test and calls `light_compute()`, which then does nothing (attenuation 0). FellowEngine returns before
the `pow()` when `distance × inv_range ≥ 1` (the attenuation is then exactly 0: the same product, `max(1 − nd⁴, 0)`),
for spot lights also when the direction is outside the cone (the cone factor is then exactly `1 − pow(1, x) = 0`),
and after the attenuation when it is at or below `HALF_FLT_MIN` (shadow and BRDF both do nothing then). Only for
materials without custom light code, rim, backlight, transmittance, clearcoat, toon shading or shadow-to-opacity (the
same exclusions as the N·L skip). lavapipe: identical on Lights Lab, PBR Lab, Stress Lab. RTX identity
(`m3_f19_temporal`, `fellow19` vs + skip): Bistro exterior 99.0 dB (max |d| 0), Sponza 75.5 dB (max 2/255), Lights Lab
90.9 dB (max 4), Bistro interior and PBR Lab at their run-to-run floor. Profile (`m3_f19_profile`): Lights Lab opaque
pass 1.709 → 1.331 ms (**−0.378**, as much as the depth slices: −0.375), both together 1.151 (−0.558); scenes without
positional lights unchanged within the profile's ±0.1 ms.

## Decision

**KEEP, both** (`cluster_depth_slices`, `zero_attenuation_skip`), **on by default** from `fellow22`: image-identical
(lavapipe bit-exact against the complete element set; RTX at every scene's run-to-run floor), −0.27 ms (−7.9 %) for
the depth slices alone and −0.47 ms (−13.5 %) together with the shadow pyramids in the Lights Lab (`m3_f20_perf`),
the opaque pass there −0.38 ms for each of the two; within noise in the scenes with no or few positional lights (the
depth slices' A/B: Bistro interior +0.035 ms p 0.24, PBR Lab −0.019 ms). The cost is the cluster packing: +0.03-0.04 ms
with 242 lights (an element near the camera covers several slices), +0.007 ms in Bistro interior.
