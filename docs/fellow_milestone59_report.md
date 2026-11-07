# FellowEngine — milestone 59 report (pipelines compiled ahead)

Status: **FINAL** (2026-11-06): checked on lavapipe, measured on the RTX (job f360). Branch `fellow/geometry`.
Decision records `docs/decisions/0161-stable-scene-bits.md`, `0162-pipeline-warmup.md`.

## 0. Result

Point 4 of the plan towards AAA: no pipeline compiled in the middle of a game. Godot never waits for a material's
ubershader, but it compiles a material's specialized pipeline when a draw first asks for it, drawing with the slower
ubershader meanwhile — every time the frame's contents change a specialization bit, and every time the camera first
sees a material. The slice's 30-minute soak with the terrain's foliage compiled 310-330 of them after its warm-up and
had three hitches of 202-262 ms during their bursts (f358).

Two changes. FellowEngine's own scene-dependent bits — absent decals, probes and lights, the positional lights' loop
variants and tile culling, the rain's wetting, the clouds' shadow — no longer follow every frame: once a feature exists
(a hidden light counts) or has been in a frame, its code stays in the pipelines, which draw the same image without it
(`frame/stable_scene_bits`, on). And `FellowPipelineWarmer3D` draws the world around the camera in six directions while
the scene loads, into a small viewport set up as the game's, until nothing more compiles. On lavapipe a scene's 21
changes compile 5 pipelines instead of 28 (the 5: a reflection probe's first capture), and a camera turning to 24
unseen objects 0 instead of 7 — with MSAA 0 instead of 13, six of which a frame had waited for.

## 1. Development

* `servers/rendering/renderer_rd/forward_clustered/render_forward_clustered.{h,cpp}`: the latched scene bits
  (`fellow_stable_seen`); `FELLOW_SPECIALIZATION_LOG`.
* `servers/rendering/renderer_rd/storage_rd/light_storage.{h,cpp}`, `texture_storage.h`: lights by type, shadowed omni
  and spot lights, decals and reflection probes counted; `effects/fellow_weather.h` and
  `modules/fellow_fluid/fellow_rain_3d.cpp`: a node that can wet the scene has existed.
* `servers/rendering/fellow_settings.{h,cpp}`, `doc/tools/fellow_settings_docs.py`: `frame/stable_scene_bits`.
* `modules/fellow_world/fellow_pipeline_warmer_3d.{h,cpp}`: `FellowPipelineWarmer3D`; icon; class reference; the
  FellowEngine menu's "Add Pipeline Warm-up"; Slice Lab `--slice_warmup=node`.
* `benchmarks/project/bench/pipeline_states_check.gd`, `pipeline_warmup_check.gd`.

## 2. Verification

* lavapipe: both checks (below); every state's picture with the setting on and off, bit for bit (21 of 21); the image
  regression suite (exact, the setting on); the editor check (the new class's icon, the menu's setup without a warning,
  undone); a 2-minute slice soak with the warm-up (no error).
* RTX 5070 Ti Laptop (job f360): both checks; Slice Lab with the setting on against off (frame-locked, interleaved,
  six runs each); the slice's 20-minute soak with the warm-up and the setting (as f358's).

## 3. Measurements

`pipeline_states_check.gd` (320 × 180; a FellowRain3D, an omni light, a shadowed spot light, a decal and a reflection
probe there from the start, dry or hidden; autotune off), the specialized pipelines compiled after the warm-up:

| | rain | omni | spot | decal | probe | night | later (rain, omni again) | all 20 changes |
|---|---|---|---|---|---|---|---|---|
| `stable_scene_bits` off | 4 | 3 | 3 | 3 | 8 | 3 | 4 | 28 |
| on | 0 | 0 | 0 | 0 | 5 | 0 | 0 | **5** |
| on, the omni light made when its state comes | 0 | 3 | 0 | 0 | 5 | 0 | 0 | 8 |

`pipeline_warmup_check.gd` (320 × 180; 24 objects in 12 materials around a camera that sees a quarter of them, turning
once around after the load), the pipelines compiled during the turn:

| | without a warm-up | with FellowPipelineWarmer3D (33 frames at load) |
|---|---|---|
| no anti-aliasing | 7 | 0 |
| TAA | 7 | 0 |
| MSAA 4× | 13 (6 while the frame waited) | 0 |

On the RTX (f360; the GPU at 67 W and 1.7 GHz that night, against 81 W and 1.9 GHz in f356):

| | result |
|---|---|
| `pipeline_states_check.gd` (1280 × 720) | off 28, **on 5** (the probe's first capture), as on lavapipe |
| `pipeline_warmup_check.gd`, TAA | the turn: 7 without the warm-up, **0** with it; the warm-up 33 frames, 341 ms, 41 pipelines |
| Slice Lab, the setting on against off (medians of five runs) | frame 5.484 against 5.450 ms, GPU 4.214 against 4.170 ms (+1.0 %, within the spread) |
| the slice's 20-minute soak, warm-up and setting on | 122 pipelines after the warm-up (6.1 a minute; f358: 10.3-11), 16 hitches |

The soak's five hitches of 167-396 ms (none compiling on the main thread, each during a burst of background
compilation) were the pipeline cache's saves holding the RenderingDevice's lock: decision 0166 (milestone 60) fixed
them — the same soak's largest frame 397 → 20 ms (f363).

## 4. Limits

* A reflection probe's first capture, particles' process materials, what the camera's place does not show (far away,
  hidden, streamed in later) and a split screen's other viewports still compile when they first draw; autotune's
  evaluations switch bits of their own (decision 0068 prepares those); Godot's own scene bits (per-light soft shadows,
  depth fog) are not latched.

## 5. Next

Other GPUs (point 6 of the plan) when the hardware is at hand; the slice's long hitches: decision 0166.
