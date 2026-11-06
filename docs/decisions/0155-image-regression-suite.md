# 0155 — An image regression suite: goldens, the release template against the editor, stock's frame; the gas volume's box where it is; the interleaved clouds' history

Status: **KEEP** (tools; editor fixes) · Branch `fellow/geometry` · Code: `tools/fellow_regression/` (`regression.py`,
`suite.json`, `project/`, `goldens/`), `tools/fellow_agent/fellow_agent.{py,gd}` (a fixed random seed); fixes:
`modules/fellow_editor/fellow_editor_plugin.cpp` (the gas volume's gizmo and handles; the Fire, Smoke and Burning Box
setups), `modules/fellow_fluid/fellow_gas_emitter_3d.{h,cpp}` (a configuration warning), `fellow_gas_volume_3d.cpp`,
`servers/rendering/renderer_rd/shaders/effects/fellow_clouds.glsl` (the interleaved trace's history bounds) ·
Checks: the suite itself; `benchmarks/tools/editor_check/` (the gas volume's height handle, an emitter below its
volume's floor).

## Problem

FellowEngine was checked change by change: captures before and after (`fellow_agent.py compare`), A/B runs on the RTX,
every option against its absence when it was made. Nothing ran again after a later change to see whether the earlier
features still drew what they drew, nothing compared a debug build's frame with a release template's — milestone 51
found the editor (a debug build) drawing no positional lights since milestone 10, because every measurement had run
release templates (decision 0152) — and the claim that FellowEngine's default options leave stock's frame was checked
per option, not again as a whole.

## Change

**`tools/fellow_regression/regression.py`** renders the scenes of `tools/fellow_regression/project/` with
`fellow_agent.py capture` from their cameras (512 × 288, 40 frames at a fixed 60 fps; `fellow_agent.gd` now seeds the
global random sequence, `--seed`, default 1; scripts and particles seed their own), group by group of settings
(`suite.json`, an `override.cfg` each), and fails a scene when

* its picture differs from its **golden** (`goldens/<group>/<scene>.png`, an editor build on lavapipe; `bless` writes
  them, `--goldens DIR` keeps another GPU's or driver's own);
* an **error** is printed while it renders;
* with `--template`, the **release template's picture differs from the editor build's**;
* in a group with `same_as`, it differs from the other group's picture: `stock` (`rendering/fellow/enabled=false`)
  against `base` (the defaults).

The default tolerance is none (identical pictures); `--tolerance` and a scene's `scene_tolerance` are for other
drivers. Groups and scenes:

| group | settings | scenes |
|---|---|---|
| `base` | defaults | `lights` (omni and spot lights with shadows, a decal, a reflection probe), `shadows` (four cascades over a 90 m street), `materials` (SSR, SSAO, SSIL, glow, volumetric fog; rough, metal, emissive, glass), `water` (FFT ocean), `rain` (drops, wet ground, puddles, a roof's shelter, a rainbow), `snow` (cover, a tracker), `lightning` (a night storm's flash), `fire` (a gas volume burning), `geometry` (a FellowHLOD3D town of 400 boxes, a MultiMesh of 4000, a 200 m ground of 80 000 triangles), `instances` (the same without the FellowHLOD3D) |
| `stock` | `rendering/fellow/enabled=false` | `lights`, `shadows`, `materials`, `instances`: the same pictures as `base` |
| `post` | atmosphere, clouds, bloom, light shafts, vignette, local exposure | `sky` |
| `rt` | ray-traced AO, sun shadows, reflections, GI (`VK_KHR_ray_query` on lavapipe) | `raytracing` |

## What building it found

1. **The gas volume's box was drawn where it is not.** A FellowGasVolume3D's box has its floor centred on the node
   (its class reference says so; the grid starts at (-x/2, 0, -z/2) in the node's space). Milestone 50's gizmo drew
   the box centred on the node, half its height too low, and its handles resized it as a box centred there; milestone 53's
   *Fire* and *Smoke* setups put their emitter 1.6 m below the volume's node and *Burning Box* its flame 1.85 m below:
   outside the box, emitting nothing — and no warning said so, so the editor check's "every setup without a
   configuration warning" passed. The suite's fire scene, placed by the gizmo, rendered an empty box. Now the gizmo
   draws the box up from the node; its handles are the width and depth (about the node) and the height (up from it);
   the setups' emitters are inside (0.4 m above the floor; the burning box's flame beside the box, 0.15 m up); and an
   emitter whose shape does not reach into its volume's box (its bounding sphere, scaled, against the box) has a
   configuration warning, updated when it moves, when its shape changes and when the volume's size does.
2. **Stock's frame is not defined at the width first chosen.** At 480 × 270 (a quarter of 1080p), `stock/materials`
   differed from `base/materials` in 137 pixels, all in the last column (x = 479) below the horizon: stock's SSAO and
   SSIL interleave passes leave the last column unwritten at widths like 480, 960 and 1920 (decision 0067), where
   FellowEngine writes it — stock's column read SSAO's unwritten (zero) values, the floor there without its ambient
   light and sky reflection. Not a regression; the suite
   renders at 512 × 288, where stock's frame is defined.
3. **The rain's splashes are not deterministic.** Two runs of the rain scene differed in 24 of 147 456 pixels (20 more
   than 8 levels, up to 98), the release template's picture from the editor's likewise; without splashes, identical. The splashes
   are stock Godot's GPU sub-emitter particles, emitted through an atomic counter in an order that changes between
   runs on lavapipe's threads (a particle's random values follow its slot). The suite's rain has no splashes.
4. **The interleaved clouds showed a checker of noise.** With the clouds traced a quarter of the pixels per frame
   (decision 0115, the default when the clouds are on), the sky scene's near clouds were dotted in a 2 × 2 pattern,
   with a still camera and without wind as well. The temporal pass clamps a pixel's history to this frame's traces in
   its 3 × 3 pixels; interleaved, a traced pixel has no other trace of this frame there, so its history was clamped to
   its own trace and lost: every pixel showed a single trace, the four of a block from four frames with four jitters.
   The bounds now come from this frame's traces of the 3 × 3 blocks around (nine traces two pixels apart). Against the
   full trace at frame 40 (512 × 288): a still camera 45.3 → 51.5 dB, a pan at 60° per second (0115's test's rate) 44.4 →
   47.5 dB (frame 41: 44.0 → 47.7 dB); the checker gone. Off (`post/clouds/interleaved=false`): unchanged.
5. **A FellowHLOD3D draws its town from other vertices with FellowEngine off.** `stock/geometry` differed from
   `base/geometry` by one level in 24 pixels, all on the town's boxes: the node builds instanced proxies (a MultiMesh
   per mesh, culled per instance) when MultiMesh culling is on and merged proxies (the transforms applied to the
   vertices) when it is off, as its class reference says. Not a stock feature: the `stock` group renders `instances`,
   the same scene without the FellowHLOD3D (identical to `base`).
6. **A release template run on a project that was never opened** prints "Could not load global script cache": the
   suite imports each group's copy with the editor build (`--import`) before the template runs it, as a game's project
   is.
7. **The geometry scene was too heavy for lavapipe**: 4000 spheres of 4096 triangles, 85 M primitives a frame with
   the shadow cascades, 4 s a frame, the capture at its 240 s timeout. Its spheres have 64 triangles now. The lightning
   scene's flash, tone-mapped linearly, clipped the ground to white: ACES and a quarter of the energy now.

## Verification

* `bless`, then `run` with the release template, twice (Linux editor and template builds, lavapipe under xvfb), after
  each fix; the goldens looked at.
* The clouds: the sky scene still and panning, interleaved against the full trace, before and after (PSNR).
* The editor check (the gizmo's new height handle dragged by mouse events and undone; an emitter below its volume's
  floor warned; every setup of the FellowEngine menu without a warning) and fellow_agent's self-test.

## Results

The final run (`run --template`, Linux editor and release template builds of milestone 54, lavapipe under xvfb; 554 s
for 32 captures), seconds per capture and the checks:

| group / scene | editor build | release template | against its golden | release against editor | against `base` |
|---|---:|---:|---|---|---|
| base / lights | 8.3 | 8.2 | identical | identical | — |
| base / shadows | 13.3 | 12.7 | identical | identical | — |
| base / materials | 20.6 | 20.0 | identical | identical | — |
| base / water | 23.4 | 23.8 | identical | identical | — |
| base / rain | 14.7 | 14.7 | identical | identical | — |
| base / snow | 14.4 | 14.5 | identical | identical | — |
| base / lightning | 5.3 | 5.2 | identical | identical | — |
| base / fire | 13.2 | 13.2 | identical | identical | — |
| base / geometry | 23.5 | 23.3 | identical | identical | — |
| base / instances | 23.5 | 23.3 | identical | identical | — |
| stock / lights | 7.5 | 7.6 | identical | identical | identical |
| stock / shadows | 11.2 | 11.2 | identical | identical | identical |
| stock / materials | 18.6 | 18.4 | identical | identical | identical |
| stock / instances | 21.6 | 21.6 | identical | identical | identical |
| post / sky | 17.8 | 18.1 | identical | identical | — |
| rt / raytracing | 33.2 | 33.5 | identical | identical | — |

No engine error in any capture. Each picture matched a golden rendered by another process earlier (`bless`), so the
pictures are the same between runs as well as between the two builds. The faults above were found by looking at the
goldens (the empty fire, the clouds' checker, the clipped flash) and by the checks (stock's last column, the splashes,
the HLOD town, the template's script cache).

| check | result |
|---|---|
| the clouds, interleaved against the full trace (frame 40, 512 × 288) | still camera 45.3 → **51.5 dB**; panning 60° a second 44.4 → **47.5 dB** (frame 41: 44.0 → 47.7) |
| the rain, two runs | with splashes 20 of 147 456 pixels more than 8 levels apart (up to 98); without, identical |
| editor check (lavapipe, X11 under xvfb) | **passed**: the gas volume's height handle 1 → 1.5 m and undone, its box drawn up from the node; the emitter below its volume's floor warned; the seven setups added, owned, **without a warning**, undone; every earlier check |
| fellow_agent self-test | **28 of 28** |

## Decision

**KEEP.** One command renders FellowEngine's features — base, stock, post-processing, ray tracing — and fails on a
changed picture, an error, a release template drawing differently from the editor, or FellowEngine's defaults
leaving stock's frame; on lavapipe every scene is bit-exact between runs and between the two builds. Run it before a
commit that touches rendering or a node (`AGENTS.md`). Building it found the gas volume's editor side wrong since
milestone 50, the fire setups emitting nothing since milestone 53 and the interleaved clouds losing their history since
milestone 19, all fixed. Not done: goldens of the RTX
(lavapipe's are the reference; `--goldens` keeps a GPU's own), the rain's splashes made deterministic (stock's
particle emission order), scenes for every option (the suite covers each group of features once).
