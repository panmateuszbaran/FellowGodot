# 0162 — Pipelines compiled while the scene loads: FellowPipelineWarmer3D

Status: **KEEP** (a new node: nothing changes without it) · Branch `fellow/geometry` · Code:
`modules/fellow_world/fellow_pipeline_warmer_3d.{h,cpp}`, icon, class reference, the FellowEngine menu's
"Add Pipeline Warm-up" (`modules/fellow_editor/fellow_editor_plugin.cpp`) · Checks:
`benchmarks/project/bench/pipeline_warmup_check.gd`; Slice Lab `--slice_warmup=node`.

## Problem

The second half of point 4 of the plan towards AAA. Godot 4.4+ compiles a surface's ubershader pipelines when the
surface enters the scene, so a draw never waits for one; but the specialized pipeline a material draws with is only
asked for by its first draw, and until it is compiled the material draws with its ubershader (slower) while a worker
compiles it. A camera that turns to materials it has not seen yet compiles them then: in the middle of the game,
in bursts. With MSAA, some of a frame's pipelines (`draw`) are even compiled while it waits. Decision 0161 stopped
what a frame holds from asking for new combinations; what is left is a material's first time in view. Godot's own
advice is to draw every material once behind a loading screen; FellowEngine had nothing for it.

## Change

`FellowPipelineWarmer3D` (a Node3D; `autostart` on) draws its world, when it is ready, into a small internal
SubViewport (`view_size`, 128 px) from the viewport camera's place (`from_camera`; or its own) in the six directions
of a cube (90° each), `frames_per_view` (3) frames each, then waits until `settle_frames` (15) frames in a row compile
no pipeline (or `max_frames`, 900), frees the viewport and emits `finished`.

The pipelines a frame asks for depend on the viewport and the camera, not only on the materials: the internal viewport
copies its own viewport's MSAA, screen-space anti-aliasing, TAA (the motion vectors it and the upscalers need),
debanding, 3D scaling mode and scale, mipmap bias, mesh LOD threshold, occlusion culling, positional shadow atlas, HDR
2D and anisotropy, and its camera the game camera's layers, range, environment, attributes and compositor. Its own
size does not enter any pipeline's key. `get_stats()` reports the frames, milliseconds and pipelines of the warm-up.
The FellowEngine menu adds one ("Add Pipeline Warm-up (compiles while loading)").

## Verification

`pipeline_warmup_check.gd` on lavapipe (320 × 180): 24 objects in 12 kinds of material (12 shaders) in a ring around a
camera that sees a quarter of them, a sun with shadows; after the load the camera turns once around in 240 frames.
Autotune off (its evaluations switch bits of their own, decision 0068).

| | without a warm-up: pipelines compiled during the turn | with FellowPipelineWarmer3D | the warm-up |
|---|---|---|---|
| no anti-aliasing | 7 specialization | **0** | 33 frames, 30 pipelines |
| TAA | 7 specialization | **0** | 33 frames |
| MSAA 4× | 7 specialization, **6 compiled while a frame waited** (`draw`) | **0** | 33 frames |

Slice Lab `--slice_warmup=node` (lavapipe, 160 × 90, a 2-minute soak at 0.6 frames a second): it runs without an
error; whether it covers the slice is the RTX's to measure. The editor check: the class's icon; the menu's setup added
without a configuration warning and undone.

Not measured yet (no GPU this time): the slice on the RTX with the warm-up and decision 0161 — the specializations its
soak still compiles after the warm-up (310-330 in 30 minutes before, f358) and the hitches during their bursts.

## Decision

**KEEP.** Behind a loading screen of a few dozen frames, the materials around the camera no longer compile their
pipelines when the camera first turns to them, with MSAA no frame waits for one, and with decision 0161 the lights,
decals and rain that exist when it runs need none either.

Not done: what the camera's place does not show (beyond its range or behind walls far away, streamed in later, hidden),
particles' process materials, a reflection probe's own passes, other viewports' settings (a split screen's second
viewport needs a warmer of its own), a record of a whole play-through's pipelines replayed at load.
