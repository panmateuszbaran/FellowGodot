# 0171 — Recorded pipelines: a play-through's pipelines compiled at load

Status: **KEEP** (opt-in: nothing happens without the file; `pipelines/record` off, `pipelines/replay` on) · Branch
`fellow/geometry` · Code: `servers/rendering/renderer_rd/fellow_pipeline_record.{h,cpp}` (new),
`forward_clustered/scene_shader_forward_clustered.{h,cpp}` (`fellow_code_hash`, the record in `_create_pipeline()`,
`_fellow_replay_pipelines()` in `set_code()` and `set_default_specialization()`), `renderer_compositor_rd.cpp` (the
file written every few seconds and at the end), `rendering_device.{h,cpp}` (`fellow_vertex_format_get_attributes()`,
`fellow_framebuffer_format_get_description()`), `shader_rd.{h,cpp}` (`fellow_source_sha256()`),
`servers/rendering/fellow_settings.{h,cpp}` (the settings, `pipelines_recorded` and `pipelines_replayed` in
`RenderingServer.fellow_get_stats()`) · Checks: `benchmarks/project/bench/pipeline_states_check.gd`; Slice Lab's soak
(`benchmarks/tools/soak/run_soak.py`), with `FELLOW_PIPELINES_RECORD`, `FELLOW_PIPELINES_REPLAY` and
`FELLOW_PIPELINES_PATH` in the environment.

## Problem

The last open items of the readiness assessment's row on shader compilation: "no record of a play-through's pipelines
replayed at load", and autotune's evaluations compiling their variants after the warm-up. Decisions 0161 and 0167
(bits that do not follow what a frame holds) and 0162 (`FellowPipelineWarmer3D`: the scene drawn around the camera
while it loads) took the slice's late pipelines on the RTX from 151 to 20 in 20 minutes (milestone 62); what is left
cannot be seen from the place a scene loads at: autotune's other variants, a material in an omni light's shadow, the
weathers and states a game reaches later. None makes a draw wait (the ubershader draws meanwhile), but each is a
pipeline compiled in play — a CPU spike, GPU memory, and frames drawn with the slower ubershader — and on a player's
first run the driver's pipeline cache is empty. Engines that ship AAA games keep such a record (a "PSO cache" of the
pipelines a QA play-through met) and create those pipelines at load.

## Change

A run with `rendering/fellow/pipelines/record` on writes every pipeline the scene shaders (Forward+) compile to
`rendering/fellow/pipelines/path` (`res://fellow_pipelines.json`). A pipeline is stored under a 64-bit hash of its
material shader's code (the same for StandardMaterial3D's generated code and for code materials in every run) with its
key: cull mode, primitive, pass version, colour pass flags, the three words of specialization constants, wireframe,
ubershader — and its vertex and framebuffer formats described (the vertex attributes; the attachments, passes, view
count and VRS attachment), because their IDs are numbered in the order a run creates them. RenderingDevice gained two
getters for those descriptions. The record is merged with what the file holds, deduplicated, and written by the render
thread three seconds after the first new pipeline (a game stopped from the editor is killed) and when the renderer
finishes. The editor process records nothing; an exported game that cannot write its own files records into `user://`.

With `rendering/fellow/pipelines/replay` on (the default; nothing happens without the file), when a scene shader's code
is set at load, the pipelines recorded for its hash are requested from the shader's pipeline map in the background at
low priority (`compile_pipeline()`, Godot's own path for the pipelines it compiles when a mesh is loaded), their formats
made from the descriptions (RenderingDevice returns the existing format when a run makes the same one later). A draw
then finds the pipeline compiled, or being compiled, instead of asking for it.

* **Tied to the scene shader.** The file holds a SHA-256 of the scene shader's source and the defines the project's
  rendering settings give it (`ShaderRD::fellow_source_sha256()`, without the engine's commit, so that the same shader
  in a later build still replays). A file of another source or settings is not replayed (a warning), and recording
  starts it again: the specialization constants' layout is in that source.
* **Replayed again after the default specialization.** The renderer sets the default specialization (the project's
  shadow and filter qualities) once after the default materials are made, and that clears every shader's pipelines:
  the first version's replay of the default material was dropped there (4 of the states check's pipelines compiled in
  play). `set_default_specialization()` now replays each shader's record after clearing it.
* **Empty framebuffer formats are not kept**: their samples are not part of their key, and making one ahead could give
  a later request other samples than a run without the file.

Also in milestone 63 (the same row): Godot's shader baker now asks every node of an exported scene for the shaders it
will make from its own setup (`FellowBakeShaders::register_object_provider()`): `FellowBurnable3D` gives its burning
materials' codes, made from its mesh's materials as `_make_burn_material()` makes them. Decision 0169's providers give
codes that do not depend on a scene.

## Verification

* `pipeline_states_check.gd --stable=0` (lavapipe, 320 × 180: the states compile the most without decision 0161's
  bits): recorded (55 pipelines of 5 shaders), then replayed — the 21 states' pictures the same, bit for bit.
* The image regression suite: no file, nothing replayed; every picture exact.
* A Linux export (release template, the shader baker on) of a scene with a burnable log, started from an empty user
  directory: the file in the `.pck`, its 23 pipelines of 3 shaders replayed, nothing recorded, no shader compiled at
  the first start — for the burnable in the scene and for one a script adds to another scene of the export (the same
  code). The same export without a scene holding a burnable baked 753 shader variants instead of 781 and compiled the
  burning material at the first start.

## Results

**The states check** (`--stable=0`, autotune off): the specialized pipelines each state compiled.

| | start | rain | omni | spot | decal | probe | night | omni, rain again | after the warm-up |
|---|---|---|---|---|---|---|---|---|---|
| without the file | 9 | 4 | 3 | 3 | 3 | 8 | 3 | 2, 2 | 28 |
| replayed | 0 | 0 | 0 | 0 | 0 | 0 | 0 | 0, 0 | **0** |

**Slice Lab's soak** (lavapipe, 160 × 90, 8 minutes: the weather every 30 s, the fire lit again every 20 s; the
terrain, its foliage, the warm-up with its shelf; autotune on; the editor build): the first run recorded 84 pipelines,
the second replayed them.

| | pipelines compiled in the soak | after its warm-up | draws with the ubershader (process) | the first frame's pipeline waits | load to the first frame |
|---|---|---|---|---|---|
| recording (as without the file) | 18 | 8 | 268 | 6, 18.9 s | 41.5 s |
| replayed | **0** | **0** | **7** | 5, 14.6 s | 47.9 s |

The replayed pipelines compile while the scene loads, on lavapipe's two vCPUs with an LLVM compiler: the load took
6.4 s longer, the first frame waited 4.3 s less for its pipelines. On the RTX laptop's 24 cores the same 84 pipelines
compile beside the load on worker threads; its numbers are for its next soak.

## Decision

Keep. A game records once per build of its content (a QA play-through through its weathers, lights and places),
ships the file in `res://`, and its players' runs compile those pipelines at load instead of in play. Without the file
nothing changes.

## Rejected / not done

* **Keying shaders by their RID or resource path**: RIDs differ every run, materials made from code have no path; the
  code's hash names the same shader everywhere.
* **The engine's commit in the file's key** (as Godot's shader cache does): every rebuild of the engine would drop the
  record although the scene shader is unchanged; the scene shader's source and defines are what the keys depend on.
* **Not yet**: the Mobile renderer's scene shader, canvas, sky and particle shaders (their pipelines are few and made
  with their shaders); a format that depends on the GPU (a VRS method another GPU does not have) makes another
  framebuffer format there, and its pipelines are compiled in play as without the file.
