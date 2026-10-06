# FellowEngine — milestone 4 report (render-thread CPU)

Status: in progress (2026-10-06). Branch `fellow/geometry`. Benchmark machine: Acer Predator PHN16S-71, Intel Core
Ultra 9 275HX (8 P-cores, 16 E-cores), RTX 5070 Ti Laptop GPU (driver 617.14), Windows 11, 1920×1080.

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

City Lab, between processes (4 ABBA runs each, `m4_f49_city_perf`):

| | frame ms | render thread ms | GPU ms |
|---|---:|---:|---:|
| stock 4.7.2 | 3.043 | 2.427 | 1.61 |
| FellowEngine, CPU toggles | 2.620 (−13.9 %) | 1.873 (−22.8 %) | 1.67 |
| FellowEngine, CPU toggles + hot workers | **2.454 (−19.4 %)** | **1.680 (−30.8 %)** | 1.73 |

(further results in progress)

## 5. Image verification

(to be completed)

## 6. Next

(to be completed)
