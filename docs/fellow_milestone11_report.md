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

Light tile culling (0049) ran after the main queue's wait for the async compute part, on the critical path, although it
needs only the depth pre-pass and the light clusters. v6 splits it: the tiles' depth bounds are recorded before the
async part (which leaves the depth buffer in the layout both queues sample), the light tests run beside it. The same
masks; on lavapipe split and whole give the same images (0 differing pixels). RTX, against culling off
(`m11_f153_tcs_ab`): Bistro interior **−1.9 %** GPU split against −0.2 % for the whole pass, Lights Lab −6.9 % against
−6.5 %; PBR Lab and City Lab, where autotune leaves culling off, unchanged by the split. Default (with async compute).

City Lab's sequences then differed from one run to the next by 1 level in a few distant pixels. Looking for the cause
found a real gap in the render graph: FellowEngine records a frame as several graphs (async compute and early
submission split it), each a command buffer, and a resource written in one graph and used again with the same usage in
a later one got no barrier and no execution dependency (1 220 such uses in 81 frames of Lights Lab). 0054 records, per
resource, the graph and stages of its last write and adds the missing barriers (and, as a diagnostic, a full barrier per
command buffer). Cost: none measurable (`m11_f156_sb_ab`, every scene, p ≥ 0.16). On by default.

It was not the cause of the deviation: in ten City Lab runs (`m11_f156_aa`) the two runs of each split configuration are
bit for bit, without barriers too, and one of the two runs of the whole pass deviates at the same pixels. A run of any
configuration can deviate. The cause found: Godot draws with the ubershader while a specialized pipeline compiles, and a
sequence's camera brings new material and light combinations into view (on the RTX: 23 compilations during Sponza's
sequence, 3 during Lights Lab's, none during Bistro exterior's). The bench now records the compilations during each
sequence and can run each sequence once before capturing it (`--temporal_warm=1`); `m11_f160` repeats the A/A pairs so.

## 3. Volumetric fog's density pass (0053, 0056)

The density pass lights every froxel; in Lights Lab (240 positional lights) it takes 0.20 ms with async compute. Two
attempts:

* **0053, light loops walked by the subgroup** (the union of the warp's cluster masks, one light at a time for all
  invocations, as the scene shader does): lavapipe bit for bit; RTX Bistro exterior bit for bit, Sponza at its floor,
  Lights Lab slightly below it (90.9 dB against 91.8-100.5, at most 4 levels). The pass −5 % in Lights Lab, which the
  frame does not show, slower in Sponza and Bistro exterior (+0.3 to +0.5 % GPU). **REJECT**.
* **0056, the loops a frame does not run left out** (specialization constants: no area lights, no VoxelGI, no
  directional light, from the light cluster and the frame's counts): lavapipe 0 differing pixels (and with six area
  lights, where the area constant is not set: 0; negative control 713 549 pixels). RTX sequences: Bistro exterior bit
  for bit, Lights Lab and Sponza at their floors. The density pass **−21 to −27 %** (Lights Lab 0.200 → 0.159 ms,
  Sponza 0.100 → 0.073, Bistro exterior 0.140 → 0.104); GPU −0.9 % in Lights Lab (p 0.001), elsewhere within noise
  (the pass runs beside the async part). **KEEP**, on by default.

## 4. Tone mapping (0055)

Forward+'s tone mapping shader holds FXAA, seven glow levels with bicubic filtering, five blend modes, five tone
mappers, colour correction, debanding and auto exposure behind push-constant tests, and is compiled for the largest of
them. 0055 gives every test a specialization constant set from the value the shader would test (one pipeline per
combination in use); stock's pipeline sets none. lavapipe: 0 differing pixels in three scenes (negative control 1.1
million). RTX sequences: City Lab bit for bit, the others at their floors. The pass **−12 to −14 %** (0.115-0.147 →
0.101-0.127 ms) in six scenes; GPU −0.3 to −1.1 % (significant in five). **KEEP**, on by default.

## 5. The colour passes without absent features (0057)

The largest pass of every scene, the opaque colour pass, compiles the decal loop, the reflection probe loop, the
directional lights' block, the omni and spot loops and the positional shadow lookups whether or not the frame has any;
0048 found its main variants at 96 registers with spills. 0057 leaves out each that the frame's light cluster and
light counts say cannot run (six bits of the colour pipelines' specialization constants; stock already does this for
area lights). The main variants drop to 72-80 registers (Sponza, Bistro interior, City Lab, Foliage Lab), the code to a
third or two thirds of its size.

**The largest gain of the milestone** (`m11_f159_abs_ab`, in one process): GPU Sponza **−9.2 %** (opaque pass −18 %,
transparent pass −26 %), Bistro interior −6.7 %, Bistro exterior −6.0 %, City Lab −5.7 % (its frame is limited by the
CPU), Foliage Lab −4.4 %, PBR Lab −3.5 %, Lights Lab −1.0 % (its main variant keeps omni, spot and shadow code and its 96
registers).

The first version was not exact: lavapipe found 1-2 isolated pixels of one level in three scenes, and the RTX City Lab
sequence 95.6 dB where two runs are bit for bit. The cause: leaving a block out lets the compiler fuse a multiply before
it with an add after it (one rounding instead of two), which stock's compiler cannot do across the block's phi. v2
passes the values the block could have changed through `fellow_keep`, an identity the compiler cannot see through (an
XOR with a zero from a UBO): lavapipe then gives **0 differing pixels** in every scene and configuration tested
(derivatives after `discard`, undefined in Vulkan, aside). RTX: `m11_f162`.

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
