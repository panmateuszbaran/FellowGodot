# FellowEngine — milestone 12 report (the queues, loading, synchronization)

Status: **DRAFT** (`fellow143`-`fellow151`; final measurement pending). Branch `fellow/geometry`. Benchmark machine as
before (Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11, 1920×1080,
"high" preset). Decision records `docs/decisions/0058`-`0062`; user-facing summary `docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

(final measurement pending)

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

## 7. Tooling

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

## 8. Incidents

* `m12_f175_load` was stopped by a pause on 2026-10-18 at 08:48 after Bistro exterior (its other scenes are in
  `m12_f176_load`).
* Disk cleanup on 2026-10-18 (asked for by the user): 125 old builds (17.1 GB), the raw run data of milestones 6-11's
  sessions except the finals and the stock references (their summaries stay), stock's build objects (4.1 GB) and the
  bench's shader cache (2.9 GB). List: `.tools/logs/cleanup_2026-10-18.txt`.

## 9. Next

* **Large surfaces at the LOD of their parts (0063, started).** Godot chooses a mesh LOD per instance; a large surface
  the camera stands in (Sponza's ivy and curtains, a terrain) is drawn at full detail to its far end. The mesh clusters
  already hold every LOD's ranges per node: each cluster can take the level stock would give it as an instance of its
  own, drawn crack-free by also drawing the finer levels of its neighbours (lavapipe terrain: 86 % fewer triangles,
  differences only at the horizon's silhouette). It changes the image as LOD does: an option, measured on the RTX in
  `m13_f179`.
* **The run-to-run floors** (Bistro interior 67 dB, PBR Lab 73 dB, Sponza 75 dB, Lights Lab 100 dB) limit how finely an
  exact change can be verified; `m13_f180` compares A/A pairs of stock, of the defaults and without async compute.
* **Frame times under background load** (City Lab's 99th percentile in `m11_f168`): `m13_f181` measures stock and
  FellowEngine with 12 processes spinning beside them.
* The post-processing chain (TAA, tone mapping, glow, auto exposure: 0.5 ms of Bistro interior's 3.2 ms) and SSIL's
  blur and interleave passes, now on the main queue.
