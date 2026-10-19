# FellowEngine — milestone 12 report (the queues, loading, synchronization)

Status: **FINAL** (`fellow143`-`fellow153`; final measurement `m12_f178_final`, 2026-10-19, with `m12_f182`-`m12_f184`). Branch `fellow/geometry`. Benchmark machine as
before (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11, 1920×1080,
"high" preset). Decision records `docs/decisions/0058`-`0062`, `0064` (`0063` started); user-facing summary `docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

Between processes (`m12_f178_final`, 3 runs per build, frame time, 1080p "high"; `fellow142` is milestone 11's final
build, `fellow151` this one's before 0064, both with their defaults):

| scene | stock (ms) | m11 | **m12** | m12 against m11 | m12 with the visually lossless options |
|---|---:|---:|---:|---:|---:|
| Sponza | 13.54 | 6.26 (−53.8 %) | **6.24 (−53.9 %)** | −0.4 % (n.s.) | 6.07 (−55.1 %) |
| Bistro exterior | 6.99 | 4.33 (−38.1 %) | **4.26 (−39.1 %)** | −1.6 % (n.s.) | 4.09 (−41.5 %) |
| City Lab | 3.12 | 2.10 (−32.5 %) | **2.14 (−31.4 %)** | +1.7 % (n.s.) | 2.13 (−31.5 %) |
| Bistro interior | 4.24 | 3.29 (−22.6 %) | **3.35 (−21.1 %)** | +1.9 % (n.s.) | 3.13 (−26.2 %) |
| Lights Lab | 3.09 | 2.36 (−23.7 %) | **2.44 (−21.1 %)** | +3.5 % (p 0.025; see below) | 1.87 (−39.4 %) |
| Foliage Lab | 3.66 | 2.96 (−19.2 %) | **2.96 (−19.0 %)** | +0.3 % (n.s.) | 2.93 (−20.0 %) |
| PBR Lab | 2.60 | 2.22 (−14.9 %) | **2.19 (−15.9 %)** | **−1.1 %** | 2.03 (−22.2 %) |
| Stress Lab | 4.99 | 4.80 (−3.8 %) | **4.92 (−1.4 %, n.s.)** | +2.5 % (n.s.) | 4.65 (−6.9 %) |

Foliage Lab with one MultiMesh per type: 10.91 → 4.88 ms (−55.3 %, `m12_f178_foliage_single`).

The frame rate did not move between processes; what moved is the time to the first frame and two reads that were not
synchronized:

* **Loading (0061): a scene's build to its first frame −12 to −62 % against stock** (FellowEngine had been 0.1-1.0 s
  slower than stock in every final since milestone 4). Section 5.
* **SSIL on the main queue beside the async SSAO (0060):** Bistro interior −2.7 % GPU, Bistro exterior −0.8 % in one
  process with autotune on (`m12_f182_split_auto_ab`); between processes within the scenes' spread (`m12_f182_regress`,
  five runs each: Bistro interior `fellow142` 3.374, `fellow151` 3.336 ms).
* Two synchronization fixes found by Vulkan validation: driver callbacks' stages (`fellow147`) and a barrier for a read
  from a stage the last write was not made visible to (0062).

Lights Lab's +3.5 % against m11 is not a change of the code (`m12_f183`, section 7): light tile culling, chosen by
autotune, came on either at frame 4462 or at frame 9391 of a run, a coin toss in every build from `fellow142` to
`fellow151`; this final drew the late case three times out of three, m11's final the early one. 0064 (`fellow153`)
continues such an undecided evaluation at once: the culling is on for at least 79 % of every run since (39 % before).
Bistro interior's +1.9 % and City Lab's +1.7 % are within their spread (five runs: section 4; `m12_f182_regress`).

Everything on by default is exact: lavapipe bit for bit, and on the RTX at the run-to-run floors (`m12_f177_vb_warm`).

**Frame-time spikes in City Lab.** City Lab's runs with FellowEngine have had frames of 16-39 ms since milestone 10
(22-183 frames over 6 ms per run, 1 % low 54-197 fps) while stock's have none (1 % low 214-239 fps); the GPU time and the
render thread's time of those frames are normal. Their excess time matches, run by run, the render thread's waits for
FellowEngine's present thread (`frame_present_thread_wait_usec`) and the time spent in `vkQueuePresentKHR`: section 10,
measured next (`m13_f185`).

## 1. The question

Milestone 11 left the colour passes compiled for the frame's features and asked next: what the second queue does while
the main queue waits (0060), what milestone 11's specializations cost while they compile (0059), and why Sponza's depth
pre-pass costs what it does (0058). Along the way the load time came into view: FellowEngine took 0.1-1.0 s longer than
stock from the start of a scene's build to its first frame, measured in every final since milestone 4 and never
questioned (0061). Vulkan synchronization validation, run on every lab for the first time, found two reads without the
barrier that makes their data visible (0060's layouts, 0062).

## 2. Large surfaces front to back (0058, REJECT)

Sponza's depth pre-pass takes 1.1 ms for about two million triangles. Drawing each large surface's clusters front to
back (by their bounds' distance to the camera) takes 2 % off the pre-pass and nothing off the frame (`m12_f169_f2b_ab`):
the pass is not spent on fragments a nearer part of the surface covers later, but on the triangles themselves (the
curtains' 2 million, the ivy's 4.9 million, the trees' bark), each covering a few pixels or none.

## 3. While a specialization compiles (0059, REJECT)

Godot draws with the 168-register ubershader while a draw's specialized pipeline compiles. 0059 drew instead with a
compiled sibling whose other bits give the same image (0057's bits cleared, tile culling off, the pyramids' early-outs
off). Where bits change after their pipelines were compiled (PBR Lab, Lights Lab) the ubershader disappears; where
materials come into view for the first time (Sponza, Stress Lab) the extra pipeline it asks for doubles the compilations
and delays the ones needed (Sponza's first run: 3357 ubershader draws against 275). Left in, off.

## 4. SSIL on the main queue (0060, KEEP)

With async compute the screen-space effects (SSAO, SSIL, SSR) run on the second queue while the main queue renders
shadows, the light clusters and the fog. In Bistro interior the main queue's part is short and it waits 1.06 ms of a
3.11 ms frame. 0060 records SSIL on the main queue, beside the async SSAO (and SSR, where autotune measures it faster:
mode 3, the default). The normal buffer is sampled by all three now (`texelFetch`), so that no queue changes its layout
while the other reads it. GPU in one process (`m12_f173_split_ab`): Bistro interior −2.7 % (SSIL on main) / −3.8 % (and
SSR), Bistro exterior −1.1 / −0.7 %, Sponza −0.6 / +0.3 %. Exact.

Testing mode 3 with Vulkan validation found that the images' layouts, set before the async part by no-op driver
callbacks, used BOTTOM_OF_PIPE as the barrier's destination stage (callbacks had no stages): every frame, and the main
queue's SSIL did not wait for the transition. A callback's command now has its resources' stages (`fellow147`).

Between processes the gain is within the scenes' spread: five runs each (`m12_f182_regress`), Bistro interior
`fellow142` 3.374 ms, `fellow147` 3.375, `fellow151` 3.336, `fellow151` with stock's layout 3.320; Bistro exterior
4.309 / 4.247 / 4.315 / 4.402. In one process with autotune on (`m12_f182_split_auto_ab`), against stock's layout: mode
1 −2.0 / −0.6 % GPU (Bistro interior / exterior), mode 2 −2.9 / −1.1 %, mode 3 −2.7 / −0.8 %. The final's Bistro
interior +1.9 % against m11 was its run-to-run spread (some runs leave async compute off by autotune's frame-time
measurement, which moves the render thread's time by 0.1-0.3 ms).

## 5. Loading (0061, KEEP)

Every final since milestone 4 recorded the time from the start of a scene's build to its first frame, and FellowEngine's
was 0.1-1.0 s longer than stock's in every scene; nobody had looked. Counters of where loading spends its time
(`fellow148`, `m12_f174_load`) put it in the first frame: the render thread creating shader objects from the shader
cache one after another, 80-247 of them at 4-5 ms each, because the first frame enables the scene shader's advanced
group (motion vectors, for TAA) and Godot then creates the group's 17 variants for every material, of which a frame uses
one or two. FellowEngine disabled took as long: its code is in the shaders whatever its settings.

0061 creates a version's variants read from the cache in parallel (the calling thread and the workers that start in
time; the driver's part outside the device's lock on Vulkan) and only when first asked for. The same shader modules.

| scene | stock | before (0061 off) | **now** | now against stock |
|---|---:|---:|---:|---:|
| Bistro exterior | 3.78 s | 4.27 | **3.08** | **−18 %** |
| Bistro interior | 1.81 | 2.39 | **1.27** | **−30 %** |
| Sponza | 3.78 | 4.53 | **3.31** | **−12 %** |
| Foliage Lab | 0.91 | 1.06 | **0.66** | **−27 %** |
| PBR Lab | 0.86 | 1.21 | **0.51** | **−41 %** |
| Stress Lab | 0.53 | 0.80 | **0.24** | **−55 %** |
| City Lab | 0.48 | 0.63 | **0.32** | **−34 %** |
| Lights Lab | 0.28 | 0.46 | **0.11** | **−62 %** |

(`m12_f176_load`, build to first frame, three runs each after a pass that filled the shader caches.) The first frame
itself: 0.07-0.28 s, against stock's 0.24-0.79 s.

## 6. A read the last write was not made visible to (0062, KEEP)

Vulkan synchronization validation on Lights Lab (lavapipe) reported light tile culling's compute shader (0049) reading
the light buffers after the copy that updated them without a memory barrier. Stock's usage tracking adds a barrier only
when a resource's usage changes; the cluster pass's shaders had read the buffers first with the same usage, and the
barrier before them covered only their stages. 0062 records the stages each write was made visible to, and gives a read
from another stage a memory barrier. No measurable cost (`m12_f177_vb_ab`, every scene, p ≥ 0.18); the RTX images and
the run-to-run floors are unchanged (`m12_f177_vb_warm`). On.

## 7. Autotune's coin toss (0064, KEEP)

The final measured Lights Lab 3.5 % slower than milestone 11 (GPU 2.287 → 2.367 ms), with light tile culling (0049,
chosen by autotune) on in 3442-3705 of 8813 frames against 8266-8746. Three runs each of five builds from `fellow142` to
`fellow151` with the verbose log (`m12_f183_lights_bisect`) gave 8266 or 3442 frames in every build, `fellow142`
included, and no difference of frame time between the builds; in one process with autotune off the culling saves the
same in both (`m12_f183_tc_ab`: −5.5 % GPU with `fellow142`, −5.8 % with `fellow151`).

The log shows why. Tile culling starts off and is switched on only by an evaluation that measures it faster. Its first
evaluation (frames 4261-4462) measures it 0.06-0.11 ms faster with a standard error of 0.03-0.05 ms, t 2.2-3.6 against
the 2.5 required: decided in some runs, not in others. Undecided, it waited behind four other tunables' evaluations,
each followed by the duty limit's pause (four times its length), and came again 5000 frames (12 s) later.

0064 (`fellow153`): a tunable that starts off, left off by an undecided evaluation whose pairs favour it, is evaluated
again next (as soon as the duty limit allows, at most three times in a row); and a frame of an evaluation in which a
draw used the ubershader or the render thread created a pipeline or shader is not counted. Lights Lab, eight runs each
(`m12_f184_lights`): the culling's share of a run 39-100 % before (mean 80 %), 79-100 % now (mean 88 %); the continued
evaluation at frame 5266 ends early with the culling on (0.16-0.22 ms). The frame times are within this session's
spread (+0.3 %, p 0.91: each build's first run 10 % slower than its others). Unchanged where the culling costs (Bistro
interior: never continued) and with the visually lossless options (Lights Lab +0.3 %, PBR Lab +0.1 %, n.s.).

## 8. Tooling

* Load-time cost counters (stats `cost_*`: shader version starts and waits, variants compiled, shaders created,
  pipelines created on and off the render thread, pipeline waits, GPU readbacks, texture creation); the bench records
  them when the scene is built and after its first frame (`fellow_stats_scene_built`, `fellow_stats_first_frame`).
  `.tools/scripts/load_times.py`, `load_costs.py`.
* Autotune's verbose log names the tunable of each evaluation.
* The bench counts a PrimitiveMesh's triangles once per mesh: City Lab's 17 000 instances had read their meshes back
  from the GPU 57 000 times during its build (3.5 s of stock's 3.8 s).
* The bench project allows 2048 timestamp queries per frame (profiled Stress Lab runs had written a 570 MB error log).
* `.tools/rbproj`: GPU readback probe (2000 mesh-array reads; stock, FellowEngine and FellowEngine disabled cost the
  same, 175-210 µs each).
* `.tools/scripts/autotune_log.py` (autotune's evaluations from verbose run logs), `spikes.py` (frames over a threshold
  per run, their excess time, 1 % low, and the waits the FellowEngine counters recorded), `spikes_frames.py` (the same
  per frame, for runs with `--fellow_frames=1`).

## 9. Incidents

* `m12_f175_load` was stopped by a pause on 2026-10-18 at 08:48 after Bistro exterior (its other scenes are in
  `m12_f176_load`).
* Disk cleanup on 2026-10-18 (asked for by the user): 125 old builds (17.1 GB), the raw run data of milestones 6-11's
  sessions except the finals and the stock references (their summaries stay), stock's build objects (4.1 GB) and the
  bench's shader cache (2.9 GB). List: `.tools/logs/cleanup_2026-10-18.txt`.

## 10. Next

* **City Lab's frame-time spikes** (section 0): measured next, `m13_f185` (stock, the defaults, without the present and
  submit threads, without the submit thread; per-frame counters).
* **Large surfaces at the LOD of their parts (0063).** Measured in `m13_f179` (KEEP as an option, off: Sponza GPU
  −3.1 %, render thread +0.21 ms, the image as LOD changes it; no effect in the other scenes).
* **The run-to-run floors** (Bistro interior 67 dB, PBR Lab 73 dB, Sponza 75 dB, Lights Lab 100 dB) limit how finely an
  exact change can be verified; `m13_f180` compares A/A pairs of stock, of the defaults and without async compute.
* **Frame times under background load**: `m13_f181` measures stock and FellowEngine with 12 processes spinning beside
  them.
* Sponza's transparent pass (1.2 ms: the cypress trees' 1.35 million alpha-blended leaf triangles) and the
  post-processing chain (TAA, tone mapping, glow, auto exposure: 0.5 ms of Bistro interior's 3.3 ms).
