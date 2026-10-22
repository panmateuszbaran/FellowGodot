# FellowEngine — milestone 14 report (the same frames on every run)

Status: **FINAL** (`fellow159`-`fellow162`; final measurement `m14_f213_final`, 2026-10-22). Branch `fellow/geometry`. Benchmark machine as before (Acer Predator PHN16S-71, Core
Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11, 1920×1080, "high" preset). Decision records
`docs/decisions/0070`-`0072`; user-facing summary `docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

**The same frames on every run.** With milestone 14's defaults two runs of FellowEngine at a fixed frame rate render
the same frames bit for bit in every scene the run-to-run floors are measured in, with autotune off; stock's two runs
differ at 69-98 dB (section 3). Three decisions close the last sources: textures cleared before their first use
(0070: no image depends on what memory held), the shadow atlas's reallocation tolerance in the scene's time at a
fixed frame rate (0071: the wall clock had decided at which frame a light's shadow changed size), and Movie Maker
mode waiting for its pipelines (0072: no draw on the ubershader in a recording). Also with the separate render thread
(bit for bit in all four scenes, section 4). With autotune on, three of the four scenes are bit for bit; Sponza differed
in one run of three while autotune compiled pipelines for an evaluation it was preparing.

Frame times between processes (`m14_f213_final`, 3 runs per build, 1080p "high"; `fellow158` is milestone 13's final
build, `fellow162` this one's). The machine ran 3-8 % slower than in milestone 13's final for every build, stock
included:

| scene | stock (ms) | m13 | **m14** | m14 against m13 | m14 with the visually lossless options |
|---|---:|---:|---:|---:|---:|
| Sponza | 13.97 | 6.89 (−50.7 %) | **6.66 (−52.3 %)** | −3.3 % (n.s.) | 6.47 (−53.7 %) |
| Bistro exterior | 7.06 | 4.58 (−35.2 %) | **4.52 (−36.0 %)** | −1.3 % (n.s.) | 4.36 (−38.2 %) |
| City Lab | 3.35 | 2.21 (−34.2 %) | **2.24 (−33.3 %)** | +1.4 % (n.s.) | 2.25 (−32.9 %) |
| Lights Lab | 3.28 | 2.45 (−25.3 %) | **2.48 (−24.3 %)** | +1.3 % (n.s.) | 1.89 (−42.2 %) |
| Bistro interior | 4.46 | 3.51 (−21.3 %) | **3.49 (−21.8 %)** | −0.7 % (n.s.) | 3.25 (−27.1 %) |
| PBR Lab | 2.80 | 2.25 (−19.5 %) | **2.29 (−18.0 %)** | +1.8 % (n.s.) | 2.05 (−26.8 %) |
| Foliage Lab | 3.90 | 3.14 (−19.5 %) | **3.20 (−17.9 %)** | +2.0 % (n.s.) | 3.15 (−19.1 %) |
| Stress Lab | 5.29 | 5.08 (−3.9 %) | **5.00 (−5.4 %)** | −1.5 % (n.s.) | 4.99 (−5.6 %) |

Foliage Lab with one MultiMesh per type: 11.31 → 5.09 ms (−55.1 %, `m14_f213_foliage_single`). As expected the
milestone's changes cost nothing measurable (each was measured on its own: 0070 in `m13_f200`).

Load to the first frame against stock (the same runs): Lights Lab 282 → 114 ms (−60 %), Stress Lab 571 → 259 (−55 %),
PBR Lab 936 → 511 (−45 %), Sponza 7489 → 4134 (−45 %; one stock run 9867), City Lab 503 → 340 (−32 %), Bistro
interior 2008 → 1373 (−32 %), Foliage Lab 923 → 652 (−29 %), Bistro exterior 4014 → 3180 (−21 %).

**Open: City Lab's frames over 6 ms came back in this session** for both FellowEngine builds (`fellow162`: 0, 33, 30
per run, 1 % low 296, 127, 124 fps; `fellow158`: 12, 30, 26; stock 1, 8, 0, 183-206 fps), in runs limited by the CPU
(frame 2.2-2.3 ms, GPU 1.4-1.6 ms) — where milestone 13's final had none in three runs and `m14_f207` none outside
the first run. The milestone's code is not the difference (m13's build has as many tonight); what the machine was
doing around them is not known (section 5).

## 1. The question

Milestone 13 found where the run-to-run floors came from (two reads in stock's SSAO/SSIL code, 0067) and left
FellowEngine's runs bit for bit in three of the four scenes it measures them in; Lights Lab kept a few pixels, and
lavapipe still found two reads of memory no pass wrote (the reflection probes' atlas, the positional shadow atlas at a
shadow's tip). This milestone closes both and asks what a rendering engine needs to give the same frames on every run.

## 2. Textures cleared before their first use (0070, KEEP, on from `fellow160`)

Stock clears a texture a pass can write, created without data, at its first use where the driver requires it (Direct3D
12); on Vulkan it holds what its memory held. With 0070 the same on Vulkan (the transfer destination usage added for
the driver only). lavapipe with poisoned allocations: no capture changes any more (PBR Lab, Lights Lab, City Lab at
480 × 270; Lights Lab and PBR Lab at 1366 × 768, where the reflection atlas and the positional shadow atlas had shown;
PBR Lab at 1920 × 1080); on against off without poison: 0 differing pixels. Cleared: 22-29 textures by the first frame,
none per frame afterwards (perf runs with autotune on). RTX (`m13_f200`): no measurable cost on frame or load time in
any scene; the images unchanged (three floor scenes bit for bit on against off).

## 3. The wall clock in the shadow atlas (0071, KEEP, on from `fellow160`)

Lights Lab's last pixels did not come from memory (0070 on: still max |d| 1-3). Two runs against each other with one
part of the scene off at a time (`m14_f201`, `m14_f202`, three runs each): bit for bit without positional shadows or
without SSAO, not without async compute, fog, probes or decals. Stock's positional shadow atlas gives a light a shadow
of another size only 500 ms after its last allocation, measured with the wall clock; the benchmark (like Movie Maker
mode) simulates 60 frames per second while the GPU draws ~430, so the frame at which a light changed resolution moved
from run to run (without SSAO the frames were faster and the decisions fell with more margin). With 0071 the tolerance
counts the simulated time when the frame rate is fixed; at a free frame rate nothing changes.

RTX (`m14_f203_floor_0071`, `fellow160`, autotune off, the warm 60-frame sequences, three runs in pairs): **Bistro
interior, PBR Lab, Sponza and Lights Lab bit for bit, 3 of 3 pairs each**; without 0071 Lights Lab max |d| 3. Against
milestone 13's `fellow158`: the three other scenes bit for bit, Lights Lab max |d| 2 (the run-dependent part).

| two runs of one build | Bistro interior | PBR Lab | Sponza | Lights Lab |
|---|---|---|---|---|
| stock 4.7.2 (`m13_f193f`) | 69.2 dB (max \|d\| 4) | 82.0 dB (4) | 80.3 dB (1) | 98.5 dB (1) |
| FellowEngine, milestone 12 (`m13_f180`) | 67.1 dB (21) | 73.5 dB (17) | 75.5 dB (4) | 101.8 dB (1) |
| FellowEngine, milestone 13 (`fellow158`) | bit for bit | bit for bit | bit for bit | 102.5 dB (3) |
| **FellowEngine `fellow160`** | **bit for bit** | **bit for bit** | **bit for bit** | **bit for bit** |

**With autotune on** (`m14_f206_floor_autotune`, the defaults, three runs in pairs): Bistro interior, PBR Lab and Lights
Lab bit for bit in all three pairs; Sponza bit for bit in one pair, and its first run against the other two 80.7 dB
(max |d| 3, every frame of the sequence) — the other two equal to the run with autotune off. In these runs autotune
switched nothing (no evaluation started), but it prepared one (0068): 25 pipelines compiled during Sponza's captured
sequence (6 in PBR Lab, none with autotune off). Compilation is the one part of the frame whose timing comes from the
machine: a draw whose pipeline is still compiling uses the ubershader, whose output can differ in the last bits. Stock
is in the same position whenever a material is drawn for the first time. Not investigated further (the frames were
deleted with the job's raw data).

**Movie Maker mode (0072, KEEP).** A recording is a fixed frame rate too, and there the remaining timing-dependent
part (a draw on the ubershader while its pipeline compiles) costs nothing to remove: Movie Maker mode does not run in
real time. While a movie is written, a 3D draw now waits for its specialized pipeline, as Godot's 2D renderer always
does. lavapipe, PBR Lab with pipelines still compiling: 0 draws on the ubershader (34 without), two recordings bit for
bit in every frame.

## 4. The separate render thread, measured again (`m14_f204`)

Godot's thread model "separate" (`--render-thread separate`, marked experimental) runs the scene's frame on the main
thread while the previous frame renders. Milestone 4 found nothing in it for FellowEngine (City Lab −0.2 %). With
`fellow160`, three runs each, every variant reading the render metrics on the render thread:

| scene | stock | stock, separate | `fellow160` | `fellow160`, separate | separate against default |
|---|---:|---:|---:|---:|---:|
| Stress Lab | 4.99 ms | 4.92 | 4.52 | **4.22** | **−6.7 %** (p 0.009) |
| City Lab | 2.79 | 2.64 | 1.96 | 1.87 | −4.5 % (n.s.; one default run with 67 frames over 6 ms) |
| Bistro exterior | 6.06 | 6.00 | 4.38 | 4.31 | −1.6 % (p 0.03) |
| Foliage Lab | 3.72 | 3.68 | 3.02 | 3.00 | −0.4 % (n.s.) |

Stress Lab's frame is limited by its script on the main thread (400 MultiMesh transforms and a 25 600-vertex surface
rewritten in GDScript every frame), which now overlaps the rendering. A project setting, not changed by FellowEngine;
recorded in `docs/FELLOWENGINE_OPTIONS.md` as a measured recommendation for projects whose main thread does real
work.

lavapipe (captures, defaults, autotune off, demote diagnostics): two runs with the separate thread bit for bit (City
Lab); FellowEngine's render-thread work (`cpu/*`, the present and submit threads, the parallel replay) off against on,
both with the separate thread: City Lab and PBR Lab **0 differing pixels**. Safe against separate differs over the
whole frame (PBR Lab 48-57 dB, max |d| 92), and the same with FellowEngine disabled (max |d| 101): the thread model's
(likely which frame the bench's capture reads; not investigated further), not FellowEngine's. On the RTX (`m14_f210_floor_rt`, `fellow161`,
three runs with the separate thread, autotune off): Bistro interior, PBR Lab, Sponza and Lights Lab **bit for bit in
all three pairs**.

ThreadSanitizer with the separate thread (a TSAN build of `fellow161`'s code on lavapipe: City Lab and Lights Lab
captures, a short City Lab perf run): besides lavapipe's own reports, milestone 13's benign persistent-instance check
(0022), two in stock (`redraw_request()`'s flag; `Engine::frames_drawn`, read by the render thread's culling while
the main thread increments it) and one in 0071: the main thread wrote the fixed frame rate to `Engine` every
iteration while the render thread read it; it is written only when it changes now (`1f6e2b4`). The stock one in the
culling is more than a report: the scene's culling detects a light seen by several cameras in one frame (to turn off
the tighter shadow caster culling, whose shadow would be wrong for the second camera) by comparing `frames_drawn`
values, and with the separate thread two cameras of one frame could read two values. It now compares the renderer's
own frame number (the render thread's); lavapipe, Stress Lab (two viewports) and City Lab, before against after with
the default thread model: 0 differing pixels. Again with both fixes (City Lab captures; a City Lab perf run with
`30a0856`): only lavapipe's reports and 0022's benign check.

## 5. City Lab's frames over 6 ms, again (`m14_f205`)

`m13_f199_final` had none in three runs of `fellow158`; `m14_f204` had 67 in one of three of `fellow160`. Five runs of
`fellow160` with per-frame counters (`m14_f205_city_spikes`): 3, 1, 16, 0, 0 frames over 6 ms (1 % low 315, 362, 189,
330, 334 fps); five of stock: 0, 0, 0, 1, 0 (209-242 fps). Every one is a single `vkQueuePresentKHR` of 5-31 ms on the
present thread, which the render thread then waits for — the same call as milestone 13's, but now in the runs whose
render thread waited on the GPU's fences for 1.0-1.7 s (runs limited by the GPU at times), not in the runs limited by
the CPU (fence waits 0.06 s: none). Open: why the presentation of this hybrid laptop (the panel on the integrated GPU)
blocks in those runs — whether the hot workers or the present thread play a part, and whether stock's present blocks
as often (stock has no counters; its frame times show 0-1 such frames per run).

`m14_f207_city_present` (five runs each, interleaved): the defaults 2, 0, 0, 0, 0 frames over 6 ms; presenting on the
render thread 5, 0, 0, 0, 0; no hot workers 1, 0, 0, 1, 0; stock 1, 0, 1, 0, 1. The few left fall in the first run
after the heat soak (the one whose render thread waited on the GPU for 1.3-2.0 s) in every FellowEngine variant, and
stock has as many: not FellowEngine's. 1 % low 292-333 fps (defaults) against stock's 209-226. Presenting on the render
thread was faster between processes (−2.9 %, 5 of 5 pairs, p 0.054); in one process (`m14_f208_present_ab`, blocks of
30 frames, autotune off) it is not: City Lab +0.7 % (p 0.008, 240 block pairs), Bistro exterior, Lights Lab and Sponza
0.0 / 0.0 / −0.1 % (n.s.). The present thread stays on.

## 6. Next

* **City Lab's frame pacing** (section 0, 5): the frames over 6 ms come and go between sessions for every
  FellowEngine build (none in milestone 13's final, 0-33 per run tonight) and much less for stock. Each is a
  `vkQueuePresentKHR` that blocks for 5-45 ms on this hybrid laptop. Next: sessions of many short runs that alternate
  the builds and the present/submit threads, with the system's own load recorded beside them.

What milestone 13's breakdown (`m13_f194`) leaves, and what each direction would take:

* **The opaque pass** is the largest pass in every scene (0.38-1.70 ms); in the light scenes the positional lights are
  61-76 % of it (0043). The exact levers tried on the light loop since milestone 8 (prefetching, hot fields, register
  budgets) cost registers and lost; light tile culling (0049) is what worked. Next exact candidates need the shaders'
  own profile (which instructions stall), not more blind variants.
* **The geometry passes** of Sponza (depth pre-pass 1.31 ms, transparent 0.92 ms): its heavy meshes (ivy, curtains,
  trees) are double-sided, so back-facing clusters cannot be culled; per-cluster LOD (0063) is the lever and changes
  the image (an option).
* **The fixed post-processing cost** (SSAO 0.33-0.40 ms, TAA 0.20-0.24, tone mapping 0.11-0.13): exact work on SSAO
  found nothing in milestones 5-9; the visually lossless options (temporal SSAO/SSIL) already halve it where a project
  turns them on.
* **The main thread**: the separate render thread (section 4) where a project's main thread works.
