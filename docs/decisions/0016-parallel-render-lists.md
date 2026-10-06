# 0016 — Render lists built by worker threads

Status: **KEEP, on by default since `fellow56`** (measured `fellow42`-`fellow59`) · Branch `fellow/geometry` · Toggles `rendering/fellow/cpu/parallel_list_fill`,
`cpu/parallel_instance_data`, `cpu/compact_list_sort`, `cpu/parallel_list_sort` (umbrella `cpu/enabled`) · Code:
`forward_clustered/render_forward_clustered.{h,cpp}` (`_fill_render_list_range`, `_fellow_fill_render_list_parallel`,
`_fellow_make_instance_data`, `_fellow_fill_instance_data_chunk`, `_fellow_sort_by_key`, `RenderList::fellow_sort_by_key_range`),
`fellow_settings.*`.

## Problem

The scenes of milestones 1-3 are limited by the GPU; open-world and city content is often limited by the CPU: many
separate objects, each culled, sorted and set up every frame for the camera and for every shadow cascade. City Lab
(`city_lab.gd`, ~19 000 mesh instances, 300 moving carts, sun with 4 cascades, 48 omni lights) is such a scene. On the
benchmark laptop (RTX 5070 Ti Laptop, Core Ultra 9 275HX) its frame is 3.0 ms with the GPU busy 1.6 ms, and the render
thread spends 2.6 ms (`m4_f41_city_profile`, CPU time between the frame's markers):

| region (render thread) | stock ms |
|---|---:|
| Setup 3D Scene (opaque list: fill, sort, instance data) | 0.951 |
| Render Directional/SpotLight Shadows (four cascade lists: fill, sort, instance data; draw recording) | 0.930 |
| Cull (main camera and four cascades, already on worker threads) | 0.395 |
| Prepare Render Frame | 0.164 |
| everything else | 0.18 |
| **total** | **2.624** |

All three steps of list building run on the render thread, one element after the other:

1. **Fill** (`_fill_render_list`): for each culled instance, its distance and depth layer, LOD, flags, and one list element per
   surface (pointers to the surface caches, whose two 64-bit sort keys are updated in place).
2. **Sort** (`RenderList::sort_by_key`): Godot's introsort on the pointer array; every comparison dereferences two surface
   caches scattered over the heap.
3. **Instance data** (`_fill_instance_data`): for each sorted element, two 3×4 transforms and flags written to the
   mapped instance buffer, and the run lengths of equal consecutive elements (drawn as one instanced draw).

A CPU probe on lavapipe (sandbox, City Lab aerial camera, 11 000 opaque elements and 8 700 shadow elements per frame)
splits them roughly 25 / 20 / 55 % (fill / sort / instance data) for the opaque list and 20 / 30 / 50 % for the shadow
lists: 4.0 ms of a 5.2 ms render-thread `_render_scene` there.

## Change

Four toggles, each exact (same lists, same keys, same order, same instance buffer words, same draws):

* **`parallel_list_fill`**: worker threads fill contiguous ranges of the culled instances (256 per range) into their own
  lists; the ranges are joined in order. Every instance appears once in the list being filled and the loop body only
  writes to that instance and its surfaces, so the ranges are independent. What the loop updated in shared state
  (scene flags such as "uses screen texture", the primitive counter) is collected per range and applied after the
  join. Lightmap capture slots are numbered in list order, so instances that need one are set aside and given their
  slot after the join, in list order (the loop's only order-dependent step). The loop body is stock's, moved into
  `_fill_render_list_range` and used by both paths.
* **`parallel_instance_data`**: worker threads write the instance data of contiguous ranges of the sorted elements and,
  per element, stock's test "same draw as the previous element"; one pass over those flags then sets the run lengths
  and counts the draws as stock does.
* **`compact_list_sort`**: the same introsort (`SortArray`) on records (key2, key1, element) copied out of the surfaces.
  The algorithm sees the same keys at the same positions, takes the same steps and leaves the same order, ties
  included; no surface is dereferenced during the sort.
* **`parallel_list_sort`** (with the compact sort, lists of 4096+ elements): introsort splits a range with one partition
  step and then sorts the two parts separately, and what it does to a part depends only on that part's elements and its
  depth budget. The partition steps above a grain size are taken level by level (in parallel within a level), the
  remaining parts are sorted by introsort itself on worker threads, and the final insertion pass runs over the whole
  list, as in `SortArray::sort`: the same steps on the same subranges.

Lists below 1024 instances or elements stay single-threaded (stock's loop).

## Verification

* lavapipe, all four toggles off vs on, default thresholds: City Lab 3 cameras and the 60-frame `city_pan` sequence
  (moving carts, TAA), Stress Lab 4 cameras: **0 differing pixels**.
* lavapipe, all four toggles off vs on, with test thresholds that split even the smallest lists (parallel from 8
  instances or elements, 7 per worker range; parallel sort from 48 elements, 24 per leaf), so that range joins, run
  lengths across range boundaries and every introsort level are exercised in every scene: PBR Lab 7 cameras and its
  300-frame sequences, Lights Lab, Blend Lab, Stress Lab, City Lab 3 cameras and `city_pan`, and City Lab with lightmap
  captures (`--city_lightmap=1`: a LightmapGI probe volume over the city and every object in dynamic GI mode, so 2048
  capture slots are given out per frame, in list order, to 11 000 instances): **0 differing pixels** everywhere. (The
  captures change the image visibly: up to 81/255 against the same cameras without them.)
* RTX 5070 Ti (`m4_f42_temporal`, seven scenes, `fellow40` vs `fellow42` and `fellow42` off vs on): Bistro exterior,
  Blend Lab bit-identical; Bistro interior, Lights Lab, PBR Lab, Sponza at their run-to-run floor in both comparisons.
  City Lab: 1-4 pixels per frame differ by 1/255 with the toggles on. `m4_f42b`: the same configuration run twice
  differs in the same way (off vs off: 445 pixels over 60 frames, all 1/255; with a 600-frame pre-roll, off vs off
  still differs while on vs on and one off/on pair are bit-identical): City Lab's own run-to-run floor, which the
  toggles stay at.

## Result

### First version: group tasks (`fellow42`, `m4_f42_city_perf`, 6 ABBA runs, reference toggles off)

| variant | frame ms | Δ (paired) | pairs faster | render thread ms |
|---|---:|---:|---:|---:|
| stock | 2.988 | −6.0 % | 6/6 | 2.377 |
| FellowEngine, toggles off | 3.179 | | | 2.527 |
| compact sort | 2.995 | −5.8 % | 5/6 | 2.352 |
| parallel fill | 3.430 | +7.9 % | 2/6 | 2.771 |
| parallel instance data | 3.569 | +12.3 % | 0/6 | 2.907 |
| compact + parallel sort | 3.938 | +23.9 % | 0/6 | 3.184 |
| all four | 3.676 | +15.6 % | 0/6 | 2.890 |

The compact sort pays off on the RTX laptop (−0.17 ms of render-thread time: there, unlike on the sandbox, the sort is
bound by the surface caches' cache misses). The parallel loops made the frame slower and noisier (run CVs 6-9 %): each
loop was a WorkerThreadPool group task with one task per thread (24), and the render thread slept until the last of
them had run; on this laptop (8 P-cores, 16 E-cores) some threads take hundreds of microseconds to start, and the
loops were 10-14 per frame. Second version (`fellow43`): the render thread processes chunks itself and a few worker
tasks (`cpu/worker_tasks`, default 6) take chunks if they start in time; the render thread waits only for chunks
being processed, never for a thread to wake.

FellowEngine with the toggles off is 6 % slower than stock here (render thread +0.15 ms, 6/6 pairs; `m4_f41`: +5.5 %):
see below.

### Second version: the render thread takes chunks itself (`fellow43`, `m4_f43_city_ab`)

Measured in one process (new perf option `--ab`): the configurations take turns in blocks of 30 frames along City
Lab's 30-second path, rotating by one block per pass, 3 rotations per run, 2 runs; blocks are paired by path position
(360 pairs per configuration). Between processes City Lab's frame time varies by up to 20 % on this laptop (where the
render thread and the workers land on P- or E-cores, clocks); within a process the per-run results agree to 0.6 %.

| configuration (reference: toggles off) | frame ms | Δ frame | render thread ms | Δ render thread | per-run Δ frame |
|---|---:|---:|---:|---:|---|
| toggles off | 3.346 | | 2.642 | | |
| compact sort | 3.300 | −1.4 % (t −2.1) | 2.604 | −0.038 ms | −1.3, −1.5 % |
| compact + parallel sort | 3.128 | −6.5 % (t −10.4) | 2.440 | −0.202 ms | −6.3, −6.7 % |
| parallel fill alone | 3.574 | +6.8 % (t 10.7) | 2.868 | +0.226 ms | +7.1, +6.6 % |
| parallel instance data alone | 3.559 | +6.3 % (t 9.5) | 2.861 | +0.219 ms | +6.5, +6.2 % |
| **all four** (6 worker tasks) | **2.817** | **−15.8 %** (t −22.3) | **2.125** | **−0.517 ms** | −16.1, −15.5 % |
| all four, render thread alone (0 tasks) | 3.307 | −1.2 % | 2.609 | −0.033 ms | −1.2, −1.1 % |
| all four, 2 worker tasks | 3.013 | −9.9 % | 2.323 | −0.319 ms | −9.8, −10.1 % |
| all four, 12 worker tasks | 2.846 | −14.9 % | 2.142 | −0.500 ms | −14.9, −14.9 % |

GPU time unchanged (−0.1 %). With all four the frame is 0.53 ms (16 %) shorter in this measurement. One of the parallel loops alone is
slower: the data a parallel step writes (surface keys, instance flags) is then read by a single-threaded step from the
other cores' caches, and the reverse; with every step parallel the lists are built where the data already is. The
render thread alone (0 worker tasks) costs nothing over the single-threaded code (−1.2 %, the compact sort's gain).

Between processes (`m4_f43_city_perf`, 4 ABBA runs, CV 0.4-1.3 %), which is what a game running one configuration
sees:

| variant | frame ms | Δ vs toggles off | pairs | render thread ms |
|---|---:|---:|---:|---:|
| stock | 3.052 | −4.2 % | 4/4 | 2.435 |
| FellowEngine, toggles off | 3.187 | | | 2.528 |
| FellowEngine, all four | **2.957** | **−7.2 %** | 4/4 | **2.235** (−0.29 ms) |

Half of the in-process gain: in one process the configurations share the CPU's clock and power state, so the
single-threaded blocks run at the lower all-core clock the parallel blocks set and the parallel blocks at a higher one
than they would get alone (this laptop's CPU and GPU share one power and cooling budget). The in-process A/B ranks
configurations of similar CPU use well; the size of a gain from using more cores is measured between processes.

Profile (`m4_f43_city_profile`, one run each, render-thread ms between markers): Setup 3D Scene (the opaque lists)
0.971 → 0.739, the shadow passes 0.947 → 0.904 (four small cascade lists, each built by three short parallel loops),
the cull 0.407 → 0.368.

In one process with the regions timed per configuration (`m4_f44_city_cpu_ab`, 2 runs, 480 block pairs): frame
−14.0 %, render thread −0.475 ms; Setup 3D Scene 1.041 → 0.733 ms, shadow passes 0.998 → 0.884 ms, cull 0.418 → 0.366
ms (the cull itself is unchanged: its data is warmer).

**FellowEngine's own render-thread cost.** Toggles off, FellowEngine's render thread is 0.09-0.15 ms slower than
stock's in City Lab between processes (`m4_f41`, `m4_f42`, `m4_f43`, 4/4 to 6/6 pairs). In one process
(`m4_f44_city_fellow_ab`, profiled, 2 runs): FellowEngine on vs `rendering/fellow/enabled = false` (stock's code
paths in the same binary) +0.032 ms (t 1.1), and none of the groups (shadows, geometry, lights, post effects) alone
accounts for more than 0.04 ms. The difference to the stock binary lies in the code both share: the list loops moved
into `_fill_render_list_range`/`_fellow_make_instance_data` (Setup 3D Scene off 0.971 ms vs stock 0.929 ms in
`m4_f43_city_profile`; the loops' outputs went through pointers the compiler had to reload, fixed in `eaded5f`) and
code layout. With the toggles on these loops run in parallel and the difference is moot (all four: −3.1 % frame vs
stock between processes).

### Shared parallel loop, parallel cull, prefetch (`fellow45`)

`FellowParallelFor` (`servers/rendering/fellow_parallel.h`) is the render-thread-first loop of the second version,
shared by the lists and the scene cull. `cpu/parallel_cull`: stock's threaded cull (a group task with one task per
thread, the render thread asleep until the last has run) processed the same way, same ranges, same per-range results
merged in the same order. `cpu/prefetch`: the list loops load instances and surfaces a few iterations ahead.

In one process (`m4_f45_city_ab`, 2 runs, 480 pairs, reference toggles off): four list toggles −16.6 % frame; + cull
−15.1 %; + cull + prefetch −14.8 %; cull alone −2.6 %; prefetch alone +0.8 % (t 1.6). The cull region itself got
slower with the cull toggle (0.449 vs 0.426 ms): 6 worker tasks for 24 ranges leave each range to fewer threads than
stock's 24 tasks. From `fellow46` the cull posts as many tasks as stock. Prefetching does not help (the hardware
prefetcher already follows these loops): off.

Between processes (`m4_f45_city_perf`, 4 ABBA runs, CV 0.2-0.6 %):

| variant | frame ms | Δ vs stock | pairs | render thread ms | Δ vs stock |
|---|---:|---:|---:|---:|---:|
| stock | 3.044 | | | 2.429 | |
| FellowEngine, toggles off | 3.169 | +4.1 % | 0/4 faster | 2.509 | +0.080 |
| FellowEngine, list toggles + cull + prefetch | **2.758** | **−9.4 %** | 4/4 | **2.069** | **−0.360** |

RTX identity (`m4_f45_temporal`, off twice and all six): Bistro exterior bit-identical in every pair; City Lab at its
floor (off vs off 108.9 dB mean, 1/255 in a few pixels; off vs on 108.8 and 101.2 dB, the same).

RTX identity (`m4_f43_temporal`, six scenes, off twice and all four): every scene at its off-vs-off floor (Bistro
exterior bit-identical; City Lab 1/255 in a few pixels per frame either way). City Lab with lightmap captures
(`m4_f43_city_lm`, three cameras): off vs off identical; off vs all four identical except one pixel of the aerial
camera, by 3/255 (followed up in `m4_f46_city_lm`: three off and two on runs).

### Batched shadow lists (`fellow46`)

`cpu/batched_shadow_lists`: stock builds each shadow pass's list when the pass is appended (fill, sort, instance data,
one pass after the other, three short loops each). The passes appended between `_render_shadow_begin()` and
`_render_shadow_process()` are now built together: all passes' instances filled by worker ranges in **record mode**
(each element gets the keys and flags its pass gives it in a record; the instances and surfaces are not written, so
passes sharing instances can be filled at once; the "previous transform" resets the fill does are applied afterwards,
the same values), sorted per pass by stock's introsort on the records, appended to the secondary list in pass order,
instance data and run lengths by worker ranges. The instance buffer is grown pass by pass as stock grows it; when
stock would have to replace the buffer in the middle of the passes (first frames, or lists outgrowing it: stock then
rewrites everything from element 0 with the keys the instances hold by then) the passes are built stock's way.

In one process (`m4_f46_city_ab`, 2 runs, 480 pairs): four list toggles −16.4 % frame, + batched shadows −18.1 %,
+ batched shadows + cull −19.0 %. Between processes (`m4_f46_city_perf`, 4 ABBA runs): four + batched + cull
**−8.9 % frame, −15.8 % render thread** vs stock (3.044 → 2.773 ms; 2.428 → 2.044 ms).

GPU-bound scenes in one process (`m4_f46_gpu_ab`, off vs all): frame and GPU time unchanged (±0.3 %); render thread
Bistro interior −18 %, Bistro exterior −1 %, Sponza and Lights Lab 0, **PBR Lab +31 % (0.30 → 0.39 ms)**: posting
and waking worker tasks for a few hundred instances costs more than it saves. From `fellow49` the batched passes use
worker tasks only from 1024 instances between them (PBR Lab, Lights Lab, Blend Lab, Stress Lab: 0 worker chunks).

Identity (`m4_f46_temporal`, `m4_f46_city_lm`): every scene at its off-vs-off floor; City Lab with lightmap captures:
of five runs (three off, two on), two off runs are identical, the third differs from them by up to 3/255 in a few
pixels, and both on runs are identical to that third off run: the 3/255 of `m4_f43_city_lm` is City Lab's own
run-to-run variation, not the toggles.

### Verification mode, fill-time sort records (`b2fc91e`, first measured in `fellow49`)

`cpu/verify` computes stock's results on the side after every list build and compares (sort order element by element,
instance buffer words, run lengths; for the batched shadow passes stock's fill per pass, its sort and its instance
data): counters `cpu_verify_checks` and `cpu_verify_mismatches`. The parallel fill also takes the opaque list's sort
records while the surfaces are in its cache, so the sort no longer gathers keys from 17 000 scattered surfaces.

### One sort for all shadow passes, hot workers (`fellow49`)

* `FellowParallelSort` (`servers/rendering/fellow_sort.h`): the level-by-level introsort of the parallel sort, for
  several arrays at once (the batched shadow passes: a large cascade is split over the workers instead of one worker
  sorting it), and SortArray::sort()'s final insertion pass run per leaf on the workers. A partition step leaves no
  element of its right part less than an element of its left part, so that pass never moves an element across a leaf
  boundary: per leaf it does what the pass over the whole array does. Checked against `SortArray::sort()` on 40 000
  random arrays (many ties, sorted and reversed input, exhausted depth budgets) and in the engine with `cpu/verify`.
* **Hot workers** (`cpu/hot_workers_usec`): the profile showed the cores barely helping (each parallel step 1.5-2×
  faster with seven threads): every loop posted its worker tasks and the workers fell asleep after it, and on this
  laptop a sleeping worker takes longer to wake than many of the loops take. The render thread's loops (cull and
  lists) now share one `FellowParallelFor` whose worker tasks stay after their last chunk, spinning, for the given time
  after the last loop, and are posted ahead when the cull starts. A loop is published by an epoch-tagged state claimed
  with a compare-and-swap, so a worker that read a finished loop can never claim a chunk of the next one with stale
  parameters. Which thread runs a chunk changes, what a chunk does does not.

RTX verification (`m4_f49_verify_*`, hot workers on): City Lab temporal (751 checks), capture (1735), lightmap
captures (1736), Lights, Stress, PBR, Blend Lab: **0 mismatches**; City Lab with hot workers: 79 % of the chunks run by
workers. Identity (`m4_f49_temporal`): Bistro exterior bit-identical; City Lab off vs off 1/255 in a few pixels, hot
vs one of the off runs bit-identical.

In one process (`m4_f49_city_ab`, 2 runs, 480 pairs, reference toggles off):

| configuration | frame ms | Δ frame | render thread ms | Δ render thread | GPU ms |
|---|---:|---:|---:|---:|---:|
| toggles off | 3.653 | | 2.806 | | 1.671 |
| all six | 2.735 | −25.1 % | 1.885 | −32.8 % | 1.649 |
| + hot workers 300 µs | 2.512 | −31.2 % | 1.621 | −42.2 % | 1.750 (+4.8 %) |
| + hot workers 1000 µs | 2.525 | −30.9 % | 1.624 | −42.1 % | 1.790 (+7.1 %) |
| + hot workers 300 µs, 10 workers | 2.474 | −32.3 % | 1.566 | −44.2 % | 1.780 (+6.5 %) |

Render-thread regions (ms, all six → + hot 300 µs): opaque fill 0.186 → 0.158, opaque sort 0.257 → 0.195 (single-threaded
0.449), opaque instance data 0.203 → 0.181, shadow lists fill 0.104 → 0.058, sort 0.267 → 0.194, instance data
0.176 → 0.168, runs 0.018 → 0.016, scene cull 0.363 → 0.357. The instance data steps barely gain from more threads:
the instance buffer is written over PCIe (device-local, host-visible memory), about 17 GB/s in total.

Between processes (`m4_f49_city_perf`, 4 ABBA runs, CV 0.3-1.3 %):

| variant | frame ms | Δ vs stock | pairs | render thread ms | Δ vs stock | GPU ms |
|---|---:|---:|---:|---:|---:|---:|
| stock | 3.043 | | | 2.427 | | 1.61 |
| FellowEngine, toggles off | 3.190 | +4.8 % | 0/4 faster | 2.524 | +4.0 % | |
| all six | **2.620** | **−13.9 %** | 4/4 | **1.873** | **−22.8 %** | 1.67 |
| all six + hot workers 300 µs | **2.454** | **−19.4 %** | 4/4 | **1.680** | **−30.8 %** | 1.73 |

The spinning workers cost the GPU 5-7 % (lower GPU clocks: this laptop's CPU and GPU share one power budget). In City
Lab the CPU is the limit and the frame is still 6 % shorter; where the GPU is the limit that would be a loss. From
`fellow50` the hot workers have a gate (`cpu/hot_workers_gate`, on): they go cold while the render thread waits for the
GPU (frame fence waits above 10 % of the frame; open again below 3 %).

### Candidate defaults, worker count, parallel partition (`fellow50`-`fellow53`)

The candidate set: the six toggles, hot workers 300 µs with the gate, compact shadow instances, 10 worker tasks, with
the present thread and parallel replay of decision 0017.

RTX verification with the candidate and the gate off (`m4_f52_verify_*`, `m4_f53_verify_*`): City Lab temporal (751
checks), captures (1735), lightmap captures (1736), draw-heavy captures (1377), Lights, Stress (7205), PBR, Blend Lab,
Bistro exterior under async compute: **0 mismatches**. Identity (`m4_f52_temporal`, six scenes, FellowEngine off twice vs
the candidate): Bistro exterior bit-identical (99 dB), every other scene at its run-to-run floor (City Lab 1/255 in a
few pixels, the same as off vs off).

In one process, City Lab (`m4_f52_city_ab`, 2 runs, 480 pairs, reference 6 workers):

| configuration | frame | render thread | partition steps ms | shadow sort ms |
|---|---:|---:|---:|---:|
| 6 workers | 2.687 ms | 1.750 ms | 0.218 | 0.176 |
| 6 workers, no parallel partition | −0.5 % (p 0.34) | −2.2 % | 0.178 | 0.149 |
| 8 workers | −1.0 % | −1.8 % | | |
| 10 workers | −2.5 % | −3.9 % | | |
| 12 workers | −3.1 % | −5.4 % | | |

The parallel partition (`fellow_sort.h`, exact) costs more than it saves at City Lab's list sizes (10-20 k elements):
its two extra parallel loops per step take longer than the serial step. It is off by default since `8155689`.

Between processes:

| session | scene | candidate vs stock, frame | render thread | pairs |
|---|---|---:|---:|---:|
| `m4_f50b_city_perf` | City Lab | −19.4 % | −31.6 % | 4/4 |
| `m4_f52_city_perf` | City Lab | **−19.2 %** (3.067 → 2.479 ms) | **−30.6 %** | 4/4 |
| `m4_f52_dh_perf` | City Lab, draw-heavy | **−17.0 %** (4.192 → 3.477 ms) | **−22.4 %** | 3/3 |
| `m4_f53_city_perf` (5 variants) | City Lab | −14.8 % (3.004 → 2.559 ms) | −31.4 % | 4/4 |

(`m4_f53_city_perf` interleaves five variants; its candidate frame is 3 % slower than in `m4_f52` at the same render
thread time: between sessions the frame varies by that much on this laptop.)

Parallel partition only for the largest ranges (`m4_f57_city_part_ab`, City Lab in one process, gate off, 2 runs):
from 16384 elements 0.0 % frame and render thread (p 0.96), from 8192 elements +0.8 % render thread (p 0.01): it stays
off.

Instance data written to the GPU buffer vs to system memory (`cpu/debug_instance_scratch`, debug: wrong images;
`m4_f57_city_scratch_ab`, City Lab in one process): the opaque list's instance data step 0.173 → 0.084 ms, the shadow
lists' 0.136 → 0.050 ms, render thread −0.161 ms (−10.7 %, p < 10⁻²⁰⁰). The writes into the mapped GPU buffer (over
PCIe on this laptop) are half of those steps' time: what a persistent instance buffer (only changed instances written)
could save.

The defaults against stock 4.7.2 between processes (`m4_f61_city_perf`, `fellow59`, 5 ABBA runs): City Lab **−16.4 %**
frame (2.777 → 2.321 ms), **−28.5 %** render thread, 5/5 pairs. FellowEngine with `enabled=false` against stock:
+3.2 % (p 0.11; +1.8 % without stock's first run, an outlier at 2.635 ms): what the disabled engine still costs (code
size, the refactored list fill), kept visible; the benchmark's per-frame FellowEngine counters, a main-thread cost
only FellowEngine builds paid, are off from `b2ef5b1`.

## Decision

**KEEP, on by default since `fellow56`** (`73f6f60`): the six toggles, compact shadow instances, hot workers 300 µs
with the gate, 10 worker tasks; the parallel partition stays off (`cpu/parallel_partition`: no gain from 16384
elements, slower from 8192, `m4_f57_city_part_ab`). Exact by construction and by verification (stock's
results recomputed and compared in every scene: 0 mismatches), −19 % frame and −31 % render thread in City Lab,
−17 % / −22 % in its draw-heavy variant (with decision 0017's end of the frame; decision 0018 takes the draw-heavy
variant further). Costs: worker threads spin for up to 300 µs after the render thread's loops while the frame is
CPU-bound (cold when the GPU is the limit). Re-measured with every default (`m4_f61_city_perf`): −16.4 % frame, −28.5 %
render thread in City Lab.
