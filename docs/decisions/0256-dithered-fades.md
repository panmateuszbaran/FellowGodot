# 0256 — Fades by distance dithered in the opaque passes

Status: **KEEP, off by default** (`rendering/fellow/geometry/dithered_fade`: it changes how fading instances look; on
with TAA or FSR, whose frames resolve the dither) · Branch `fellow/geometry` · Code:
`servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.{h,cpp}` (which fades are dithered; the
opaque lists keep them; one dither a draw), `servers/rendering/renderer_rd/forward_clustered/scene_shader_forward_clustered.h`
and `servers/rendering/renderer_rd/shaders/forward_clustered/scene_forward_clustered{,_inc}.glsl` (two specialization
bits, the dither), `servers/rendering/renderer_geometry_instance.h` (the instance's dither this frame),
`servers/rendering/fellow_settings.{h,cpp}` (`rendering/fellow/geometry/dithered_fade`),
`modules/fellow_world/fellow_hlod_3d.{h,cpp}` (`fade`), `modules/fellow_world/fellow_world_streamer_3d.cpp`
(`bake_proxies()`'s `group_fade`) · Checks: `benchmarks/project/bench/dithered_fade_check.gd` (new), the image
regression suite.

## Problem

FellowEngine's far objects hand over at a distance — an HLOD's members to their merged proxy (decision 0122), a group's
cells' proxies to the group's (0252), any node's levels of detail through Godot's visibility ranges — and every one of
them switches in a frame: a pop, the more visible the more the levels differ. Godot can fade at a visibility range
(`visibility_range_fade_mode`), but a fading instance is drawn in the transparent pass: blended, sorted, one draw per
instance (no instancing), left out of the depth pre-pass, SSAO and screen-space reflections, and — two levels crossing
over at half opacity each — see-through: three quarters of the background's colour through a solid object mid-fade.
FellowEngine had kept its hand-overs as switches for that reason (decisions 0172, 0252).

## Change

* **`rendering/fellow/geometry/dithered_fade`** (off): an instance fading by its visibility range (its own fade or
  its visibility parent's), not made transparent itself (`transparency`) and with opaque surfaces, stays in the opaque
  lists — the depth pre-pass and the opaque passes — and keeps a share of its pixels equal to its fade: those where
  interleaved gradient noise of the pixel's position, moved with TAA's frame (`taa_frame_count`), is below it, the
  same pixels in the depth pre-pass and the colour pass. TAA (or FSR) resolves the share into a blend over a few
  frames; without it the pattern stands still.
* **Crossing over without holes**: an instance fading at its range's start (the far level coming in) keeps the other
  pixels (one minus the noise below its fade). Two levels handed over at one distance with the same margin have fades
  that add to one (`smoothstep` is symmetric), the fades now rounded rather than truncated to their eight bits: every
  pixel is one level's or the other's. Under a visibility parent (`VISIBILITY_RANGE_FADE_DEPENDENCIES`) the parent is
  drawn whole behind its fading dependants.
* **Its shadows as before**: a fading instance casts its whole shadow, as stock's do (both levels while crossing).
* **Pipelines**: two specialization bits (`fellow_dither_fade`, `fellow_dither_fade_near`), set only on a fading
  instance's opaque draws; every other draw's pipeline is the one it was, without the code. Two instances are drawn in
  one instanced draw only with the same dither. The first fade of a material compiles its pipelines (the ubershader
  meanwhile, decision 0059's fallbacks do not drop the bits).
* **FellowHLOD3D's `fade`** (off): its merged proxies' members fade over `margin` on each side of `distance` (the proxy
  their parent, `FADE_DEPENDENCIES`) instead of switching; the instanced proxies still switch.
* **`bake_proxies()`'s `group_fade`** (0 m): a group's cells' proxies fade over it on each side of the group's switch.

Forward+ only (the Mobile renderer blends, as stock).

## Verification

`dithered_fade_check.gd` (lavapipe, 960 × 540, TAA): two levels of a ball (a sphere of 64 × 32 faces, and of 10 × 5 in
a darker colour) handed over at 40 m with 6 m of margin: each fading itself (`FADE_SELF`), or the far one the near
one's visibility parent (`FADE_DEPENDENCIES`); a switch, the fade blended, the fade dithered.

* Nothing fading (the camera at 20 m): the picture with the setting on is the picture with it off, exactly.
* Crossing over (15 balls at 40-44 m, without TAA): no pixel of the far level's silhouette left to the sky, dithered
  (none blended either, but the balls see-through: 0.8 % of the picture more than 16 levels off the far level alone,
  0.26 % dithered).
* The hand-over (the camera still, its distance moved from 50 m to 30 m over 150 frames; the balls' mean brightness
  inside them, 0-255): a switch steps by up to 3.55 a frame; blended by 2.52 (the far level's and the near one's
  blends do not add up to a solid ball); dithered 0.79 (`FADE_SELF`) and 0.74 (`FADE_DEPENDENCIES`) — ramps.
* FellowHLOD3D with `fade`: 16 boxes merged into a proxy, the camera at its distance: the boxes and the proxy drawn
  (15 552 + 3 888 triangles), the picture the boxes', no hole.
* The image regression suite: every picture exact (the setting off; nothing fades in its scenes).

## Results

On the RTX (job f470: 1920 × 1080, the release template, three runs, and the editor build once; the same steps,
crossings and HLOD triangles as on lavapipe): 4 000 balls on a shell 40 m around the camera, every one of them crossing
over (both levels drawn), the medians of 120 frames:

| 4 000 balls mid-fade | GPU | the renderer's CPU | draws |
|---|---:|---:|---:|
| a switch (both levels drawn whole) | 3.31-3.46 ms | 0.58-0.62 ms | 2 |
| `FADE_SELF` blended (stock) | 3.12-3.43 ms | 1.26-1.53 ms | 7 135 |
| `FADE_SELF` **dithered** | **4.05-4.12 ms** | **0.62-0.69 ms** | **2** |
| `FADE_DEPENDENCIES` blended (stock) | 2.30-2.34 ms | 0.69-0.72 ms | 2 |
| `FADE_DEPENDENCIES` **dithered** | **3.99-4.09 ms** | 0.61-0.66 ms | 2 |

All at once, the dither costs the GPU 0.6-0.7 ms more than drawing both levels whole (16 M triangles through the depth
pre-pass and the opaque pass with a discard: no early depth writes), and 0.9 ms (blended `FADE_SELF`) to 1.7 ms
(blended `FADE_DEPENDENCIES`, whose fading children skip the pre-pass) more than blending; blending pays on the CPU —
one draw a fading instance, sorted (7 135 draws, +0.7 ms). A test of the worst case: a scene's fades are the few
objects crossing their hand-over's band.

City Lab's flythrough (job f471: 1080p, the release template, 3 runs of 60 s interleaved; its 18 814 static meshes all
merged per cell beyond 100 m, `--city_hlod_instancing=0`):

| the members at 100 m | frame | GPU | the renderer's CPU | draws |
|---|---:|---:|---:|---:|
| switching (as before) | 2.15 ms | 1.64 ms | 1.27 ms | 2 073 |
| fading over 10 m on each side, blended | 2.24 ms | 1.65 ms | 1.36 ms | 2 248 |
| fading over 10 m on each side, **dithered** | 2.28 ms | 1.64 ms | 1.39 ms | 2 257 |

A fade costs a tenth of a millisecond there either way (the members and their proxy both drawn in the band); dithered
or blended, the same GPU time.

## Decision

Keep, off by default: with TAA or FSR, fades by distance stay solid (no background through a crossing, no holes),
lit and shaded as opaque, instanced, for the GPU time of drawing the fading objects as opaque ones; FellowHLOD3D's and
the streaming groups' hand-overs can fade instead of popping (`fade`, `group_fade`). Off because it changes how fading
instances look, and without a temporal resolve the dither stands still as a screen-door pattern.

## Rejected / not done

* **Alpha to coverage** (MSAA's samples as the dither): only with MSAA, which FellowEngine's frames do not use (TAA,
  FSR).
* **A dither by the material** (`ALPHA_HASH`): per material, and in object space — the two levels' hashes are not
  complementary.
* **The fades in the shadow passes**: a dithered shadow map flickers under TAA's jitter of the camera (not the
  light's); the levels' shadows overlap while they cross, as stock's do.
* **Not yet**: the instanced HLOD proxies and the foliage's impostors (handed over per instance on the GPU, decisions
  0122 and 0172) still switch; a streaming cell's proxy and its loaded content switch when the content enters.
