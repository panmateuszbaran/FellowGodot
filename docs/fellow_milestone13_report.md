# FellowEngine — milestone 13 report (frame pacing, LOD of large surfaces, run-to-run floors)

Status: **DRAFT** (`fellow152`-). Branch `fellow/geometry`. Benchmark machine as before (Acer Predator PHN16S-71, Core
Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11, 1920×1080, "high" preset). Decision records
`docs/decisions/0063`-; user-facing summary `docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

(pending)

## 1. The question

Milestone 12's final measured the frame rate of every scene where milestone 11 had left it and found what averages
hide: City Lab's runs with FellowEngine have had frames of 16-39 ms since milestone 10 (1 % low 54-197 fps against
stock's 214-239), while the GPU's and the render thread's times of those frames were normal. This milestone asks where those frames
go, what the run-to-run floors that limit exactness checks are made of, and measures the per-cluster LOD started in
milestone 12 (0063).

## 2. Large surfaces at the LOD of their parts (0063, KEEP as an option)

In one process (`m13_f179_clod_ab`, autotune off): Sponza GPU −3.1 % (6.311 → 6.113 ms), its render thread +0.21 ms (the
walk of each large surface's cluster tree in every camera and perspective shadow pass); Bistro exterior, Bistro
interior and Foliage Lab unchanged (no surface large enough reaches a coarser level on their paths). The image changes
as LOD changes it: Sponza 63.6 dB on a warm 60-frame sequence (59.3 dB at its worst frame), stills 52.9-56.8 dB, the
differences at the silhouettes of distant arches and in the ivy. An option, off.

`fellow157`: one walk per camera render — the depth pre-pass stores a surface's per-level ranges, the colour and
motion passes of the same render reuse them (lavapipe terrain: bit for bit, the same triangles drawn).

## 3. City Lab's spikes

`m13_f185_present`, four runs each (City Lab): every FellowEngine configuration has them in most runs — with the present
and submit threads (37-57 frames over 6 ms per run), presenting on the render thread (2-129), with the present thread
and without the submit thread (0-143) — and stock in none. `m13_f186_spikes`: also without async compute, with four
swap chain images, with a frame queue of three; not with FellowEngine disabled at run time (0-1). Runs whose frames are
limited by the GPU (the render thread waits on fences for 1.3-2.1 s of the run) have none; runs limited by the CPU
(fence waits 0.06-0.17 s) have them. Per frame (`m13_f186_perframe`): every spike is one `vkQueuePresentKHR` call of
6-45 ms (mean 0.42-0.46 ms per frame against 0.1 ms with FellowEngine disabled), which the render thread then waits for.

Not the frame rate either: capped at stock's 330 fps (a diagnostic pacing of the main thread) the runs have 0-104
(`m13_f188_spikes`). They vanish with the hot workers not spinning (`cpu/hot_workers_usec` 0: seven runs over
`m13_f188` and `m13_f189`, 0 frames over 6 ms, 1 % low 280-303 fps) and with the render thread's parallel work off
(`cpu/enabled` off: 1-3, 1 % low 203-212). In a CPU-bound frame of 2 ms the hot workers (ten by default) linger 300 µs
after each loop and the loops follow one another, so they spin through the whole frame; the machine has eight
performance cores; a thread the presentation needs waits a scheduler quantum. Under background load the frame is slower
and the spikes are gone too (`m13_f181_load12`: 1 per run).

What removes them (City Lab, four runs each unless noted):

| hot workers | frames over 6 ms per run | 1 % low (fps) | mean frame |
|---|---|---|---|
| 10, linger 300 µs (default) | 43-109 | 56-125 | 1.97-2.07 ms |
| yielding the core every 64 pauses (`m13_f189_city`) | 24-95 | 60-171 | 2.04-2.08 |
| 8 (`m13_f190_hot`, three runs) | 29-78 | 79-137 | 1.96-2.02 |
| 6 (three runs) | 0-8 | 259-339 | 2.01-2.04 |
| 4 (three runs) | 0-67 | 72-327 | 2.00-2.09 |
| linger 100 µs (three runs) | 97-136 | 50-63 | 2.04-2.26 |
| **linger 30 µs** | **0-1** | **325-336** | 1.99-2.11 |
| off | 0 | 280-303 | 2.26-2.40 |
| stock | 0-1 | 212-239 | 2.98-3.23 |

In one process (`m13_f189_ab`, autotune off) the yield costs City Lab 7.9 % of its mean frame and the hot workers off
17.2 % (the spikes included); Bistro exterior and interior, limited by the GPU (the hot workers gated off), unchanged.

(in progress: `m13_f191` — idle hot workers at the lowest thread priority; `m13_f192` — the linger in one process)

## 4. The run-to-run floors

`m13_f180_floor`, warm 60-frame sequences, two runs of one build against each other:

| scene | stock A/A | FellowEngine A/A | FellowEngine without async compute A/A |
|---|---|---|---|
| Bistro interior | 69.2 dB (max \|d\| 2) | 67.1 dB (21) | 67.1 dB (13) |
| PBR Lab | 82.1 dB (3) | 73.5 dB (17) | 73.5 dB (28) |
| Sponza | 80.3 dB (1) | 75.5 dB (4) | 75.5 dB (4) |
| Lights Lab | 98.4 dB (1) | 101.8 dB (1) | 103.1 dB (1) |

Stock is not bit for bit between runs either; FellowEngine's floors are lower, and not from async compute nor from
autotune (`m13_f187_floor_noat`, autotune off: 67.1 / 73.5 / 75.5 / 100.2 dB, max |d| 13 / 25 / 3 / 1). (to measure:
`m13_f195` — A/A pairs with each area of FellowEngine off)

With autotune on, the captured sequences compile pipelines although each was run once before: Sponza 50, PBR Lab 9,
Bistro interior 7, Lights Lab 2 specializations during the 60 frames (stock none; none with autotune off). Autotune's
evaluations switch pipeline bits (the shadow pyramids, light tile culling) in scene states the warm pass did not
combine them with, and those frames draw with the ubershader. Compiling the other choices ahead (0066) was tried and
removed: its low-priority tasks hung the engine's exit on lavapipe, and at normal priority they compete with the
compilations the frame needs. For exactness checks, autotune off.

## 5. Tooling

* The bench: `--fellow_frames_keys` chooses the FellowEngine counters recorded per frame (with `--fellow_frames=1`);
  `--max_fps` paces the main thread (diagnostics only: the runner's `--fixed-fps` disables the engine's own cap).
* `.tools/scripts/spikes.py`, `spikes_frames.py`.

## 6. Next

(pending)
