# FellowEngine — milestone 11 report (specialized passes, synchronization between command buffers)

Status: **DRAFT** (`fellow132`-`fellow137`; RTX measurements queued: `m11_f156`-`f159`). Branch `fellow/geometry`.
Benchmark machine as before (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only,
Windows 11, 1920×1080, "high" preset). Decision records `docs/decisions/0049` (v6), `0053`-`0057`; user-facing summary
`docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

Pending.

## 1. The question

Milestone 10 went back to stock passes that every scene runs and asked what they spend their time on. This one asks
the same of code the GPU compiles but a frame does not run: the tone mapping pass, the volumetric fog's density pass
and the colour passes each hold every feature they support behind runtime tests, and a shader's registers are
allocated for its largest live set whatever the tests say (0048: the colour pass sits at 96 registers with spills).
It also follows up light tile culling's split around async compute (0049 v6), whose images differed between runs on
the RTX.

## 2. Light tile culling around async compute, and barriers between a frame's command buffers (0049 v6, 0054)

Pending (`m11_f156`).

## 3. Volumetric fog's density pass (0053, 0056)

Pending (`m11_f156`, `m11_f158`).

## 4. Tone mapping (0055)

Pending (`m11_f156`).

## 5. The colour passes without absent features (0057)

Pending (`m11_f159`).

## 6. Tooling

* `FELLOW_SEGMENT_LOG`: the render graph lists uses of a resource written in an earlier command buffer of the frame
  with its usage unchanged; stats `rd_graph_segment_hazards`, `rd_segment_full_barriers`.
* `debug/scene_shader_experiments` 4194304: `discard` as demote to helper (image tests on lavapipe: derivatives after
  `discard` are undefined, and lavapipe's values depend on the code around them).
* Lights Lab `--area_lights=N` (tests of paths that depend on area lights).
* lavapipe with Vulkan synchronization validation (`VK_LAYER_SETTINGS_PATH`); async compute emulated on the main queue
  (`frame/async_compute=1`, `frame/async_compute_family=2`).

## 7. Incidents

* `m11_f155` (fog) was paused by the user on 2026-10-14 at 22:47: its images in Lights Lab and Sponza were complete,
  three of Bistro exterior's four runs and the in-process A/B were cut short when the processes were closed (the job
  still ended with `rc=0`). Queued again in `m11_f156`. The queue then stayed stopped while the machine was in use.

## 8. Next

Pending.
