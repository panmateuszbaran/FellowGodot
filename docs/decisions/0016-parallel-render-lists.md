# 0016 — Render lists built by worker threads

Status: **MEASURING** (`fellow42`) · Branch `fellow/geometry` · Toggles `rendering/fellow/cpu/parallel_list_fill`,
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

## Decision

(pending)
