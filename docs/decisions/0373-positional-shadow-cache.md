# 0373 — Positional shadow static cache (omni and spot lights)

Status: **KEEP** (exact on lavapipe; GPU gain to be measured on the RTX) · Branch `fellow/geometry` · Toggle
`rendering/fellow/shadows/positional_cache` (default **on**) · Code: `servers/rendering/renderer_scene_cull.cpp`
(`_fellow_pos_shadow_*`, `_fellow_shadow_events_gc`), `servers/rendering/fellow_positional_shadow_cache.h`,
`renderer_rd/effects/fellow_shadow_cache.*` (six faces, `clamp_max`), `shaders/effects/fellow_shadow_cache_copy.glsl`,
`render_forward_clustered.cpp` (`_render_shadow_pass`, `_fellow_push_shadow_copy`)

## Problem

The gap analysis after milestone 230 (`docs/fellow_gap_analysis_m230.md`) named the shadowed lights one of the three
gaps the eye sees against Unreal Engine 5.8: each shadowed omni light is a cube of six faces (or two paraboloids) drawn
again, with every caster in its range, whenever anything in that range moves — the osteria's two shadowed lights cost
1.9 ms (decision 0370). Stock Godot keeps a positional light's shadow while nothing in its range changes, but one
moving person under a chandelier re-renders all six faces with the whole room in them, every frame. The directional
light has had a static/dynamic split since decision 0004; the omni and spot lights had none.

## Change

When an omni or spot light's shadow is drawn again (stock's decision: the light is dirty and its atlas slot asks for
it), its casters are split as the directional cache splits them: a caster whose shadow depends on nothing but its
transform, mesh and material state (`fellow_shadow_cacheable`, not `GI_MODE_DYNAMIC`) and that has not changed for
`directional_cache_static_frames` frames (60) is **static**; the others are **dynamic**.

* **Per light and shadow atlas, a cache of its faces** (`FellowShadowCache`, now up to six faces per id): a cube
  light's six faces, a dual paraboloid light's two halves, a spot light's map.
* **Made again** (rebuild) when the light changed (its transform, range, angle, cull face, caster mask, the camera's
  layers, the omni mode), when a cached caster changed or left within its range (the scenario's invalidation events
  of decision 0004, with the box where it was cached), when events it had not checked were forgotten (kept 240
  frames), when casters that became static were drawn over the copy (promotion, at most once per `static_frames`), and
  when the camera has moved farther than `positional_cache_camera_travel` (4 m) from where it was at the rebuild (the
  static casters' mesh LOD follows the camera, as stock's). A light that moves is drawn the stock way until it has
  been still for `static_frames` frames (a flickering fire's light that moves would otherwise rebuild every draw).
* **Exact by construction.** The static casters are drawn exactly as stock draws them: a cube face over its whole
  framebuffer from the origin, so into the cache texture directly (same viewport, same texels, same depths); a spot
  map or a paraboloid into its own atlas rect (rasterization depends on the viewport's place in the atlas: float
  rounding of the viewport transform), then the rect copied out into the cache, and reused only while the light keeps
  that rect. Every later draw copies the cache into the face (depth as it is: `clamp_max` 1, not the directional
  cascades' 0.9999) and draws the dynamic casters over it with the stock pipeline, no clear: the depth test keeps the
  nearest of both, as if drawn together.
* The static casters of a rebuild are taken before the camera-dependent tighter caster culling (the cache outlives
  the camera); the dynamic ones after it, as stock.
* Its own counters (`shadow_pos_cache_*` in `RenderingServer.fellow_get_stats()`) and settings in a header of their
  own (`fellow_positional_shadow_cache.h`: `fellow_settings.h` is included by the whole engine).
* Reflection probes' renders draw their lights the stock way.

## Measurements (lavapipe; no GPU timings: the owner asked for no GPU work for now)

* **Exactness.** The regression suite's new `pos_cache` scene (a cube-shadowed omni light, a dual paraboloid one and a
  spot light over static boxes, cylinders and a MultiMesh ring; a ball circling between them every frame; a pillar
  hidden at frame 20, a crate moved at frame 26), casters static after 4 frames: the cache on and the cache made again
  at every draw both give the picture of the cache off, to the pixel, at frames 8, 21, 27, 33 and 40. Over 40 frames:
  315 faces drawn from the cache, 45 rebuilt (6 invalidations: the pillar and the crate in three lights; 6 promotions),
  1 256 casters left to the cache, 358 drawn over it. With the default 60 frames (a capture's 40 frames all dynamic):
  `base` and `stock` (FellowEngine off) identical. The whole suite (46 scenes): every picture identical to its golden,
  every `same_as` group to its reference. Under Vulkan's synchronization validation (the new atlas-to-cache and
  cache-to-atlas copies): no message.
* **Draws.** A tavern of 220 static meshes (tables, stools, mugs, barrels, bottles, beams) under a chandelier (cube), a
  fireplace (paraboloids) and two window spots, six walkers (a capsule and a head each), frames 100-160: the shadow
  passes' draw calls 803 → 333 a frame (−59 %), their primitives 292 600 → 254 500 (−13 %: the walkers' 4 000-triangle
  heads, drawn every frame either way, are most of them); the picture identical.
* The osteria itself on lavapipe outgrows the cloud machine's 7 GB (the editor build killed with and without the
  cache): its counts and the GPU's time wait for the RTX (`climb_look.gd --shadow_info=1`).

## RTX laptop (2026-11-27; RTX 5070 Ti Laptop, 1920×1080, 840 frames after 240, two runs each in alternating order)

The showcase's osteria at night from inside (`climb_look.gd --program=osteria --view=inside --night=1 --perf=1
--shadow_info=1`; the lamps' and the fireplace's shadows), the cache on → off:

| | cache on | cache off |
|---|---|---|
| shadow passes' draws a frame | 3 258 | 5 710 (cache: −43 %) |
| their primitives a frame | 8.45 M | 15.33 M (−45 %) |
| GPU, mean (median) | **8.10 / 8.27 ms** (8.04 / 8.16) | 8.84 / 8.85 ms (8.78 / 8.79) |
| render CPU, mean | **3.78 / 3.95 ms** | 4.17 / 4.45 ms |

The cache: −0.66 ms of GPU (−7.5 %) and −0.45 ms of the render thread a frame; 6 120 faces drawn from it and 12 rebuilt
over 840 frames.

## Decision

KEEP, on by default: the shadow maps are stock's (the regression suite's `pos_cache_*` groups), and a redraw costs
the copy plus the dynamic casters instead of every caster.

## Rejected

* **Drawing the static casters straight into a cache texture for spot maps and paraboloids** (as for cube faces):
  the atlas rect's position changes the rasterization's float rounding, so the copy would not be stock's texel for
  texel.
* **A cache per light without the atlas in the key**: two viewports giving a light slots of different sizes would
  remake it every draw.
