# 0161 — Pipelines that do not change with what a frame holds

Status: **KEEP, on** (exact: the same image) · Branch `fellow/geometry` · Setting
`rendering/fellow/frame/stable_scene_bits` (runtime, on) · Code: `render_forward_clustered.{h,cpp}` (the scene's
specialization bits), `light_storage.{h,cpp}` (lights by type, shadowed omni and spot lights), `texture_storage.h`
(decals), `fellow_weather.h` and `fellow_rain_3d.cpp` (a node that can wet the scene) · Check:
`benchmarks/project/bench/pipeline_states_check.gd`; diagnostics `FELLOW_SPECIALIZATION_LOG`.

## Problem

Point 4 of the plan towards AAA: no pipeline compiled in the middle of a game. Godot draws with a material's ubershader
while its specialized pipeline compiles in the background, so a new pipeline costs slower frames rather than a stall on
the main thread — but a burst of them is a burst of work for the worker threads, and the slice's 30-minute soak with
the terrain's foliage had three hitches of 202-262 ms during bursts of 0.6-1.9 s of specialization compiles (decision
0159, job f358); the soak compiled 310-330 specializations after its warm-up, in 30 minutes.

Many of those bursts were FellowEngine's own. Its colour passes specialize on what the frame holds: without the decal,
reflection probe, omni, spot and directional light code where the frame has none (decision 0057), the positional
lights' loop variants only with omni or spot lights in view (0045-0047), light tile culling's masks where it ran
(0049), the weather's wetting code only while something is wet (0124), the clouds' shadow while it is on (0133). Each
time one of them changes — the campfire's light lit, a lightning flash, the rain starting, the sun set — every
material in view needs a pipeline with the new combination. Logged on lavapipe over the slice (`FELLOW_SPECIALIZATION_LOG`,
new: once per pipeline a draw asked for before it was compiled, with its key): wetness, the omni lights and their loop
variants, light tile culling and the MultiMesh bit (a material on a new MultiMesh) changed between the keys of the same
material and pass. Decision 0059 had already concluded that "compiling the right specializations earlier, with the
bits the frame will use" is what would remove these frames.

## Change

With `frame/stable_scene_bits` each of these bits takes the value that compiles the feature's code as soon as the
feature exists in the RenderingServer, or has been in a frame, and keeps it for the session:

* decals, reflection probes, directional, omni and spot lights, shadowed omni or spot lights: when one exists (a hidden
  node's light counts: the RenderingServer holds it) or has been in a frame — counted by type in `LightStorage` and
  `TextureStorage`;
* the positional lights' loop variants (fields read early, prefetching, the hot fields buffer): when an omni or spot
  light exists or has been in view;
* light tile culling: once it has run, or could run (an omni or spot light exists, the frame allows it), its bit stays
  on in frames without omni and spot lights — the masks are only read for a non-zero cluster word, and there is none;
* the wetting code: once a FellowRain3D has existed (it tells the weather), dry or not — dry, the weather's data is zero
  and the code returns at once;
* the clouds' shadow: once it has been on — off, its strength is zero and its map white.

Every one of these values draws the same image in a frame without the feature (the code runs no iteration or returns at
once: decision 0059 checked the same bits as image-neutral). What it costs is the code being compiled in where the
specialized pipeline would have left it out: a few more registers, a loop entered for nothing.

## Verification

`pipeline_states_check.gd` on lavapipe (320 × 180; a sun with shadows, ground, six objects in three materials, a
MultiMesh, a transparent pane; a FellowRain3D, an omni light, a shadowed spot light, a decal and a reflection probe
there from the start but dry or hidden; autotune off, its evaluations switch bits of their own), 21 states one after
another — the rain on and off, the lights, the decal, the probe shown and hidden, the sun set and risen, then again —
each held until 20 frames compile nothing:

| | specialized pipelines compiled after the warm-up |
|---|---|
| off (as before) | 28: rain 4, omni 3, spot 3, decal 3, probe 8, night 3; and again later: omni 2, rain 2 |
| **on** | **5**, all at the reflection probe's first capture (a pass of its own: its first draws need pipelines anyway) |
| on, the omni light made only when its state comes (`--late=1`) | 8: the probe's 5, the new light's 3 the first time, 0 the second |

Each state's picture is the same, bit for bit, with the setting on and off (21 of 21). The image regression suite:
every picture exact with the setting on (its default).

On the RTX (f360): the same check gives the same counts (28 off, 5 on, the same states). Slice Lab on the terrain with
its foliage, 1080p, frame-locked and interleaved (six runs each, the first of each left out: it compiled its own
pipelines): frame 5.484 ms on against 5.450 off, GPU 4.214 against 4.170 ms (medians; +0.6 % and +1.0 %, within the
runs' spread of 4.12-4.49 ms), the 99th percentile 7.29 against 7.25 ms. (That night the GPU ran at 67 W and 1.7 GHz.)

## Decision

**KEEP, on.** A scene's features no longer recompile its materials when they come and go: on lavapipe 28 compilations
across a scene's changes become the 5 of a pass that had never run, at the same image. On the RTX the general pipelines cost the slice
about 1 % of its GPU time, within the runs' spread.

Not done: reflection probes' and other passes' first draws (their pipelines are new: rendering them once at load would
compile them); autotune's evaluations (decision 0068 prepares those); Godot's own scene bits (soft shadows per light,
depth fog), which change less often; MultiMesh and other per-instance bits (a material on a new kind of instance).
