# FellowEngine — milestone 4 report (render-thread CPU)

Status: **FINAL for `fellow59`** (`b2ef5b1`, measured 2026-10-06/07). Branch `fellow/geometry`. Benchmark machine: Acer
Predator PHN16S-71, Intel Core Ultra 9 275HX (8 P-cores, 16 E-cores), RTX 5070 Ti Laptop GPU (driver 617.14), Windows
11, 1920×1080. Decision records: `docs/decisions/0015` (async compute in CPU-bound frames), `0016` (render lists on
worker threads), `0017` (end of the frame: parallel replay, present and submit threads), `0018` (parallel recording),
`0019` (frame plan, option).

## 0. Result

The defaults of `fellow59` against stock Godot 4.7.2, between processes (ABBA, `m4_f61_*_perf`, 2026-10-06
22:28-23:46; frame = wall time between main-loop iterations, render thread = the viewport's CPU render time):

| scene | stock frame | FellowEngine frame | Δ frame | Δ render thread | Δ GPU time | pairs |
|---|---:|---:|---:|---:|---:|---:|
| City Lab, draw-heavy (3000 materials) | 3.832 ms | **2.531 ms** | **−34.0 %** | **−42.9 %** | +11 % | 4/4 |
| City Lab | 2.777 ms | **2.321 ms** | **−16.4 %** | **−28.5 %** | +8 % | 5/5 |
| Bistro exterior (with the interior) | 6.335 ms | **5.051 ms** | **−20.3 %** | −61.4 % | −7.0 % | 3/3 |
| Sponza | 13.545 ms | **7.076 ms** | **−47.8 %** | −33.0 % | −49.1 % | 3/3 |
| Lights Lab | 3.252 ms | **2.969 ms** | **−8.7 %** | +15.5 % (0.18 → 0.21 ms) | −10.2 % | 3/3 |
| Bistro interior | 4.295 ms | **4.070 ms** | **−5.2 %** | −14.0 % | −5.8 % | 3/3 |
| PBR Lab | 2.800 ms | 2.784 ms | −0.6 % (p 0.31, noise) | +17.1 % (0.26 → 0.31 ms) | −0.8 % | 2/3 |

No scene is slower than stock. The CPU-bound scenes gained most from this milestone; the GPU-bound ones keep the gains
of milestones 1-3 (Sponza, Bistro, Lights Lab) and Bistro exterior, CPU-bound in stock (7363 draws, render thread
3.3 ms), is now bound by the GPU. Every change of the milestone is exact: same lists, keys, order, instance data,
commands and submissions as stock; `cpu/verify` 0 mismatches in every scene; images bit-identical in Bistro exterior
and at each scene's run-to-run floor elsewhere (RTX), 0 differing pixels on lavapipe (§10).

On this laptop both City Lab variants now run at about GPU time + 0.5 ms, the presentation path of its hybrid graphics
(decision 0009). There the defaults' GPU time is above stock's: the GPU clocks lower while the CPU works on more cores
(the two share the laptop's power budget), and the directional shadow pyramid of milestone 3 costs City Lab more than it
saves (§9). In the small GPU-bound scenes (Lights, PBR Lab) the render thread spends 0.03-0.05 ms more than stock
(FellowEngine's fixed per-frame work), which their GPU-bound frames do not show.

## 1. Why this milestone

Milestones 1-3 made the GPU's work cheaper in scenes the GPU limits (Sponza, Bistro, the labs). Open-world and city
content is often limited by the CPU instead: tens of thousands of separate objects, each culled, sorted and set up for
the camera and for every shadow cascade, every frame. None of the earlier scenes showed that cost, so this milestone
adds one that does and works on the render thread.

## 2. City Lab (new benchmark scene, `benchmarks/project/scenes/city_lab.gd`)

Built in code, deterministic, driven only through `set_time()`: a 56×56 grid of 11 m lots, each with a building of
2-4 parts and street furniture (lamp posts, trees, crates, benches): ~19 000 MeshInstance3D nodes sharing 4 meshes and
12 materials, 603 k triangles; 300 carts moving every frame; a sun with 4 cascades (300 m), 48 omni lights, SSAO,
glow, TAA. Fixed cameras `city_aerial`, `city_street`, `city_roofs`, a 30-second flythrough and the 60-frame
temporal sequence `city_pan`. `--city_lightmap=1` adds a LightmapGI probe volume with every object in dynamic GI mode
(verification of the lightmap-capture path).

Stock Godot 4.7.2 on the benchmark laptop: frame 3.0 ms, GPU 1.6 ms, render thread 2.4-2.6 ms: CPU-bound. Where the
render thread's time goes (`m4_f41_city_profile`, CPU time between the frame's timestamp markers):

| region | stock ms |
|---|---:|
| Setup 3D Scene (opaque list: fill, sort, instance data) | 0.951 |
| Render Directional/SpotLight Shadows (four cascade lists; draw recording) | 0.930 |
| Cull (camera and cascades; already on worker threads) | 0.395 |
| Prepare Render Frame (instance updates) | 0.164 |
| everything else | 0.18 |

## 3. Method: in-process A/B

Between processes, City Lab's frame time varies by up to 20 % on this laptop (where the render thread and the workers
land on P- or E-cores, clocks): with 4 runs per variant, effects under ~5 % are hard to see. The perf mode got an
in-process A/B option (`--ab=name:k=v;k=v|...`, `tools/ab_inproc.py`): runtime-switchable FellowEngine settings take
turns in blocks of 30 frames along the camera path, rotating by one block per pass, so every configuration runs every
block of the path equally often in the same process; blocks are paired by path position (hundreds of pairs per run).
With `--profile` the timestamped regions are recorded per configuration. Per-run results agree to ~0.5 %.

Caveat found on the way: configurations that use more cores change the CPU's clock and power state for the others in
the same process, so in-process differences between single-threaded and parallel configurations come out about twice
as large as between processes (§4). In-process A/B ranks configurations; between-process ABBA runs size the gains.

## 4. Render lists built by the render thread and worker tasks (decision 0016)

See `docs/decisions/0016-parallel-render-lists.md` (method, exactness argument, verification, all tables). Every
change keeps stock's lists, keys, order (ties included), instance buffer contents and draws; `cpu/verify` recomputes
stock's results on the side and compares them after every build.

| step | what changed |
|---|---|
| compact sort | stock's introsort on (key2, key1, element) records instead of pointers to scattered surfaces |
| parallel fill | worker ranges of the culled instances, joined in order; lightmap capture slots given out afterwards in list order |
| parallel sort | introsort's independent parts on workers; several arrays at once; the final insertion pass per leaf |
| parallel instance data | worker ranges; run lengths from per-range run starts |
| batched shadow lists | all shadow passes of a frame filled (record mode), sorted, set up together |
| parallel cull | stock's threaded cull with the render thread taking ranges itself |
| hot workers | worker tasks stay awake between the render thread's loops; cold while the GPU is the limit |
| compact shadow instances | shadow passes write the 112 of 176 instance bytes a shadow pass reads |

City Lab, between processes (ABBA runs):

| session | | frame ms | render thread ms | GPU ms |
|---|---|---:|---:|---:|
| `m4_f49_city_perf` (4 runs) | stock 4.7.2 | 3.043 | 2.427 | 1.61 |
| | CPU toggles | 2.620 (−13.9 %) | 1.873 (−22.8 %) | 1.67 |
| | CPU toggles + hot workers | 2.454 (−19.4 %) | 1.680 (−30.8 %) | 1.73 |
| `m4_f52_city_perf` (4 runs) | stock 4.7.2 | 3.067 | 2.442 | |
| | candidate (+ present thread, parallel replay, 10 workers) | **2.479 (−19.2 %)** | **1.695 (−30.6 %)** | |
| `m4_f52_dh_perf` (3 runs, draw-heavy: 3000 materials) | stock 4.7.2 | 4.192 | 3.168 | |
| | candidate | **3.477 (−17.0 %)** | **2.457 (−22.4 %)** | |

## 5. The end of the frame (decisions 0017, 0019)

After the scene is recorded the render thread replays the render graph into the command buffer, submits and
presents (0.2 ms per frame in City Lab, 0.5 ms in the draw-heavy variant, 1.4 ms in Bistro exterior).

* **Parallel replay**: large draw lists are replayed into secondary command buffers by the workers, each segment
  starting with the state in effect at its start. Only while the frame is CPU-bound (it cost Bistro exterior 1.6 % GPU
  time).
* **Present thread / submit thread**: `vkQueuePresentKHR`, and with the submit thread every `vkQueueSubmit` of the
  frame (a FIFO of jobs, also async compute's three extra submissions), issued by a dedicated thread. Its first version
  gave nothing in one process: the render thread waited for the whole FIFO at the start of the next frame; it now waits
  only for the submission of the frame slot it reuses (`fellow54`). Gated to CPU-bound frames like the replay (it cost
  Sponza 2.2 % GPU time).
* **Frame plan** (0019, `fellow57`, option): all draw lists of the frame split before the primary command buffer is
  recorded and replayed by the workers at once while the render thread records the rest. Exact (RTX: verification and
  identity clean); −2.7 % in the draw-heavy City Lab in one process, but no difference between processes (−0.2 %) and a
  little more GPU time in City Lab: kept as an option, off.

## 6. Recording on several threads (decision 0018)

The draw recording of a large list (`_render_list_template()`: the pipeline, uniform sets, buffers, push constant and
draw of every element) was the largest single step left on the render thread in the draw-heavy City Lab (depth pre-pass
0.49 ms, opaque pass 0.41 ms in one process). The draw list is forked into segments, each recorded by a worker with its
own state tracking and instruction buffer from a run start, and joined in order; the replay then splits the list where
it was recorded. Lists of 384 draws or more (a run of instanced repeats counts as one draw: City Lab's instanced lists
stay serial). Draw-heavy City Lab with every default: **−34 % frame, −43 % render thread** vs stock
(`m4_f61_dh_perf`). Granularity (`m4_f57_dh_seg_ab`): one recording segment per worker and at most 12 replay segments
per list are best; more segments only add re-binds and secondary command buffers.

## 7. Async compute in a CPU-bound frame (decision 0015)

In City Lab async compute costs about 3 % of the frame even with the submit thread (`m4_f53_city_perf`); in the
GPU-bound scenes it gains 0-2 %. It stays an option, off by default.

## 8. The separate render thread, measured again

Stock's `--render-thread separate` (thread model "separate", marked experimental) runs the scene tree's frame on the
main thread while the previous frame renders. `m4_f55_city_rt` had measured it slower (stock +6.1 %). That result was
the benchmark's: every frame the main thread read the viewport's render times and draw counts
(`viewport_get_measured_render_time_*`, `viewport_get_render_info`), and with a separate render thread each such read
waits until the render thread has drained its queue, i.e. finished the previous frame, which serializes the two
threads. From `b29c58e` the benchmark can read them on the render thread (`--rs_metrics=render_thread`, a call queued
behind the frame: the same values, no wait); `m4_f61_city_rt` uses it for every variant (4 ABBA runs):

| City Lab | frame | vs stock |
|---|---:|---:|
| stock | 2.858 ms | |
| stock, separate render thread | 2.769 ms | **−3.1 %** (p 0.03, 4/4) |
| FellowEngine defaults | 2.325 ms | −18.6 % |
| FellowEngine defaults, separate render thread | 2.320 ms | −18.8 % |

Draw-heavy City Lab (`m4_f61_dh_rt`, 3 runs): stock 3.765 ms, stock with the separate render thread 3.674 ms (−2.4 %,
p 0.24), defaults 2.553 ms (−32.2 %), defaults with the separate render thread 2.541 ms (−32.5 %).

The separate render thread helps stock a little and FellowEngine not at all here: City Lab's main thread does little
(0.3 ms), and with FellowEngine the frame is already at the GPU plus presentation limit (§9). Also fixed on the way: at
exit, the main RenderingDevice was finalized by the main thread after the render thread had ended, which failed its
render-thread guard and leaked the device's resources (also in stock; `a24492e`).

## 9. Where the frame ends now: GPU time and the shared power budget

Both City Lab variants now run at GPU time + ~0.5 ms (City Lab: 2.32 ms frame, 1.77 ms GPU; Bistro exterior 5.05 / 4.67
ms): the presentation of this hybrid-graphics laptop (the panel is driven by the Intel iGPU; decision 0009 found no way
for the engine to hide it). Two consequences:

* Render-thread savings no longer shorten the frame much here. The instance data written into the GPU buffer, for
  example, costs 0.17 ms of render thread per frame in City Lab over PCIe (`m4_f57_city_scratch_ab`: written to system
  memory instead, debug, the render thread is 10.7 % faster) but the frame only 2.1 %.
* The defaults' GPU time is above stock's in City Lab (1.77 vs 1.64 ms, +8 %; draw-heavy +11 %), with the GPU clock
  lower in every FellowEngine run (~2030 vs ~2180 MHz; laptop telemetry of `m4_f61_city_perf`): the CPU's worker threads
  and the GPU share the laptop's power budget, and part of FellowEngine's GPU work in City Lab is its directional shadow
  cache (cascades copied into the atlas every frame the camera moves, 0.10 ms, and their pyramid, 0.05 ms, where stock
  renders City Lab's instanced shadows in 0.07 ms). Which defaults cost GPU time there (`fellow59` defaults vs one
  change, ABBA, 4 runs each; `m4_f62_*_power`, `m4_f63_*_wait`):

| change against the defaults | City Lab frame | GPU | draw-heavy frame | GPU |
|---|---:|---:|---:|---:|
| no directional pyramid (`shadows/directional_max_mip`) | **−2.7 %** (4/4) | **−3.2 %** | **−2.0 %** (4/4) | **−4.3 %** |
| no directional shadow cache | −0.5 % (n.s.) | −0.5 % | −0.7 % (n.s.; render thread +3.8 %) | −1.7 % |
| hot workers linger 100 µs instead of 300 | −0.2 % (n.s.) | −0.4 % | **+2.8 %** (render thread +3.2 %) | −1.0 % |
| 6 worker tasks instead of 10 | −0.7 % (n.s.) | −0.4 % | +1.0 % (render thread +2.3 %) | −0.7 % |
| idle workers in UMWAIT C0.1 (`cpu/hot_workers_wait=1`, `5702851`) | −0.8 % (n.s.) | −0.2 % | −0.5 % (n.s.) | +0.2 % |
| idle workers in UMWAIT C0.2 | **−1.1 %** (4/4) | −0.4 % | **+1.7 %** (0/4) | −0.3 % |

  The spinning workers are not what lowers the GPU clock: idle workers waiting in a low-power state (the CPU has
  WAITPKG; ~23 million waits per run) change the GPU time by less than 0.5 %, and the deeper state's slower wake costs
  the draw-heavy variant more than it gives City Lab: `cpu/hot_workers_wait` stays an option, off. The linger and the
  worker count stay as they are. What does cost GPU time in both City Lab variants is the directional shadow pyramid
  (milestone 3, decision 0005): its build (0.05 ms) saves nothing in City Lab's opaque pass, while in Sponza it saves
  14 %. Exact either way, so the engine could choose it per scene by measuring (§12).

## 10. Image verification

Every build of this milestone ran with `cpu/verify` (stock's lists and instance data recomputed on the side and
compared): 0 mismatches in City Lab (plain, draw-heavy, lightmap captures), Lights, Stress, PBR, Blend Lab and Bistro
exterior, also under async compute and with the frame plan (`m4_f57_verify_*`). Identity against FellowEngine with the
milestone's work off (temporal sequences of six scenes and the draw-heavy `city_pan`, captures; `m4_f52_temporal` to
`m4_f57_temporal`): Bistro exterior bit-identical, the other scenes at their run-to-run floor (the same values as off vs
off; one draw-heavy frame at 92.9 dB in `m4_f56_temporal_dh` was between the two "off" runs, not the defaults).
lavapipe (CPU rasterizer, deterministic): 0 differing pixels for every step.

## 11. Measurement problems (kept visible)

* **GPU throttled after a pause** (2026-10-06 18:45-21:00): the laptop stood on a different surface; under load the
  GPU reached its 87 °C target and dropped to P2/P4 states (memory clock 9-10.8 instead of 14 GHz, ~65 W), so stock
  City Lab's GPU time rose from 1.6 to 2.5 ms (`m4_d57_thermal`). The frame-level runs of `fellow56` from that window
  were stopped and discarded (City Lab had become GPU-bound); verification and identity results from it are kept.
  Per-run GPU telemetry in the logs (clock, temperature, power) is how it was noticed: checked before every analysis.
* **The separate render thread's earlier result** (§8) was produced by the benchmark's own per-frame reads.
* **Per-frame FellowEngine counters** cost every FellowEngine variant main-thread time (a dictionary of ~80 counters
  built every frame; stock builds have none): off from `b2ef5b1` (`--fellow_frames=1` turns them back on). With them,
  FellowEngine disabled measured +2.6 % vs stock (`m4_f58_city_perf`); without, +3.2 % (p 0.11; +1.8 % without stock's
  first run, an outlier): what the disabled engine still costs is kept visible in 0016.
* In-process A/B ranks but can overstate: the frame plan's −2.7 % in one process did not show between processes.
* Recording by elements instead of draws (`fellow54`) forked City Lab's instanced lists for nothing (+3 %); fixed
  before the defaults were set.

## 12. Next

* **Exact optimizations chosen by measurement**: the directional pyramid costs City Lab 2.7 % and saves Sponza 14 %
  (§9); the frame plan, the shadow cache and the parallel replay are likewise exact and scene-dependent. Toggling any of
  them between frames changes no pixel, so the engine can run them in alternating blocks of frames, compare its own GPU
  timestamps and keep the faster choice per scene, re-checking now and then (the pyramid already rebuilds itself whole
  when turned back on).
* **Persistent instance data**: every frame each list element's 176 bytes of instance data (112 for shadow passes) are
  written into a mapped GPU buffer, over PCIe on this laptop: 0.17 ms of render thread per frame in City Lab. Most of
  it does not change between frames (static instances); a persistent per-surface buffer updated only where the data
  changed, with a per-frame map of 16 bytes per element (slot, flags, GI offset: the per-pass fields), would cut that
  to the changes. Needs the scene shaders to read through the map (behind a specialization constant) and a
  verification of the persistent contents against stock's per-element data.
* **The directional shadow cache's GPU cost in instanced scenes**: City Lab's static casters are cheap to render (0.07
  ms) and the cache's copies and pyramids cost more (0.13 ms); the cost gate measures casters, not GPU time.
* The parallel cull (0.22-0.26 ms in City Lab) is limited by its work per range, not its balance: smaller ranges
  (`cpu/cull_ranges_per_thread`, exact) made it slower (`m4_f61_city_cull_ab`: +11 % render thread with 4 per thread);
  what is left there is stock's per-instance work (frustum and cascade tests, light pairing).
