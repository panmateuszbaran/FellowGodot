# FellowEngine — milestone 14 report (the same frames on every run)

Status: **DRAFT** (`fellow159`-). Branch `fellow/geometry`. Benchmark machine as before (Acer Predator PHN16S-71, Core
Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11, 1920×1080, "high" preset). Decision records
`docs/decisions/0070`-; user-facing summary `docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

(pending)

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
(likely which frame the bench's capture reads; not investigated further), not FellowEngine's.

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
thread was faster between processes (−2.9 %, 5 of 5 pairs, p 0.054): measured in one process next (`m14_f208`).

## 6. Next

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
