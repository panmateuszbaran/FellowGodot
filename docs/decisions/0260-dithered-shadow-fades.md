# 0260 — Fading instances' shadows fade with them

Status: **KEEP** (with `rendering/fellow/geometry/dithered_fade`, decision 0256, for the fades by distance; always for
an instance asked to keep a share of its pixels, decisions 0258-0259) · Branch `fellow/geometry` · Code:
`servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.{h,cpp}` (the shadow passes' fades, by the
main camera; a specialization per shadow element; the view's TAA frame), `servers/rendering/renderer_scene_cull.{h,cpp}`
(fading instances out of the directional shadow caches, their positional lights' shadows drawn again),
`servers/rendering/renderer_geometry_instance.{h,cpp}` (an HLOD's members known to the renderer),
`modules/fellow_world/fellow_world_streamer_3d.cpp` (a swap's outgoing mesh casts its shadow) · Checks:
`benchmarks/project/bench/dithered_fade_check.gd` (section 8), the image regression suite.

## Problem

Decisions 0256-0259 made FellowEngine's hand-overs fade — levels of detail by distance, HLOD members into their
proxies, streamed cells into theirs, a streamed mesh's stand-in into the full mesh — dithered in the opaque passes. Their
shadows did not: a fading caster cast its whole shadow until the frame it was gone, then none. Every hand-over still
popped, on the ground beside it: a tree's or a house's shadow appearing or vanishing at once, or, crossing over, both
shadows drawn (the content's and its proxy's, slightly different shapes). Stock's shadow passes have no fade at all
(they measure the fade from the light, and a fading instance is cast whole).

## Change

* **The shadow passes dither the fades the view does**: in a directional cascade, a spot light's or an omni light's
  dual paraboloid, an instance fading by its visibility range (with `geometry/dithered_fade`; measured from the main
  camera, as in the view) or keeping a share of its pixels (decision 0258's `fellow_instance_set_dither_alpha()`) keeps
  that share of its shadow map's texels, by the same noise (moving with the view's TAA frame) and on the same side: two
  instances handing over at one distance (their fades adding to one) write every texel once — one shadow, the shape of
  whichever covers it. The shadow filter and TAA resolve the dither into a shadow of that depth.
* **A specialization per shadow element**: the shadow lists carry each element's dither (two bits beside its level of
  detail), set by the pass's own fill — a caster the camera does not see fades too — and two elements are drawn in one
  instanced draw only with the same dither. Every other shadow draw keeps its pipeline.
* **No pixel left, not drawn**: a caster faded out entirely is left out of the shadow pass (a positional light's
  casters include those their range hides — stock casts their shadows whole).
* **Shadow maps kept up to date**: a fading instance is changed every frame of its fade and once after it — out of the
  directional caches meanwhile (decision 0004: drawn with the cascades' dynamic casters, each cache holding it
  invalidated once), and its positional lights' shadows drawn again, as for a moving instance.
* **Not faded**: an omni light's cube map (its depths are filtered into the atlas's paraboloids, which averages a
  dither's texels with what lies behind them: the shadow is whole while it fades, gone when it is); an HLOD's proxies and
  members (decision 0122: their shadows are per cascade, not by the camera's distance); a MultiMesh's per-instance fades
  (decision 0257: their shadows per cascade).
* **A streamed mesh's swap** (decision 0259): its outgoing mesh now casts the node's shadow, dithered against the
  node's — one shadow while they cross over.

## Verification

`dithered_fade_check.gd` section 8 (lavapipe, 960 × 540, without TAA: the dither stands still): a tall box on the
ground fading out at 40 m (its own fade, 6 m each side), lit by the sun (its shadow cached), by an omni light's dual
paraboloid or by its cube map; the camera moved only, 20, 40, 60, 40 and 20 m away; the depth of the box's shadow
against the box whole and absent at each distance (1 the whole shadow, 0 none):

| the box at | 20 m (whole) | 40 m (half faded) | 60 m (gone) | 40 m | 20 m | handing over at 40 m | 40 m, the setting off |
|---|---:|---:|---:|---:|---:|---:|---:|
| the sun (cached) | 1.00 | 0.42 | 0.00 | 0.42 | 1.00 | 1.00 | 1.00 |
| an omni light (dual paraboloid) | 1.00 | 0.46 | 0.00 | 0.46 | 1.00 | 1.00 | 1.00 |
| an omni light (cube map) | 1.00 | 1.00 | 0.00 | 1.00 | 1.00 | 1.00 | 1.00 |

The shadow fades with the box and back (no stale shadow map, cached or positional), two boxes handing over cast one
shadow, the setting off keeps stock's whole shadow; with nothing fading the picture with the setting on is the picture
with it off, exactly. A streamed ball's swap (section 7's, on a ground in the sun) keeps its whole shadow halfway in
both directions (0.997 and 1.002). Sections 1-7 as before; the image regression suite exact.

## Results

On the RTX (job f475: 1920 × 1080, the release template, three runs each, interleaved with milestone 134's build):
section 8 as on lavapipe (the sun's shadow 1.00 → 0.42 → 0.00 and back, the omni light's 0.46, one shadow handing over,
a streamed mesh's swap 0.997 and 1.001), sections 1-7 as before. The cost, 4 000 balls (8 000 instances, two levels
each) all mid-fade on a shell 40 m around the camera, the sun's shadows on:

| all mid-fade, the sun's shadows on | GPU | the renderer's CPU | shadow draws |
|---|---:|---:|---:|
| switching (no fade) | 5.84-6.71 ms | 1.10-1.23 ms | 16 |
| blended (stock's fade: whole shadows) | 7.81-8.04 ms | 1.96-2.06 ms | 7 149 draws in all |
| dithered, whole shadows (milestone 134) | 6.81-6.87 ms | 1.23-1.30 ms | 16 |
| **dithered, shadows dithered** | **6.79-6.95 ms** | **1.83-2.03 ms** | 16 |

The GPU's time is the same; the renderer's CPU +0.6 ms for 8 000 fading casters (about 75 ns each): they are drawn into
the cascades every frame of their fade instead of staying in the shadow cache, and marked so (without shadows the
marking alone +0.35 ms for 8 000 — cut afterwards to the positional lights' part when no directional light casts
shadows). A frame's few dozen fading instances cost nothing measurable: Frontier Lab streamed with the atmosphere, its
cells and regions crossing over in 0.5 s (3 runs of 90 s interleaved):

| Frontier Lab, crossing over | frame | GPU | the renderer's CPU | shadow draws | p99 |
|---|---:|---:|---:|---:|---:|
| their shadows switching (milestone 134) | 4.24 ms | 3.80 ms | 0.92 ms | 136.6 | 5.62 ms |
| **their shadows crossing over too** | **4.15 ms** | **3.71 ms** | 0.94 ms | 140.5 | 5.52 ms |

Within the runs' spread (4.15-4.29 ms and 4.08-4.28 ms); four more shadow draws (the cells crossing over drawn with the
cascades' dynamic casters for half a second).

## Decision

Keep: with `geometry/dithered_fade` (and with `crossfade`) a hand-over no longer pops on the ground either. Opt-in
with the fades themselves; nothing changes for shadows of instances that do not fade.

## Rejected / not done

* **The cube map's shadows dithered with a coarser pattern**: the cube's half-resolution faces are filtered into the
  paraboloids at any size; whole while fading, as before, for the cube's omni lights.
* **A separate setting**: the shadows of `geometry/dithered_fade`'s fades are part of what the setting fades; a
  dithered object with a whole shadow was the pop this removes.
* **The fade measured from each light** (stock's shadow passes): the view's fade is what the camera sees; a shadow fading
  by the light's distance would not match its caster.
