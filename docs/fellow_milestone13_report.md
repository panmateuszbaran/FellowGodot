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

**What lavapipe can say about them.** Two runs of FellowEngine on lavapipe are bit for bit (PBR Lab's warm sequence,
60 frames): what differs between runs on the GPU is what lavapipe makes deterministic — the contents of memory no pass
wrote and the order of racing stores. With new allocations filled with a constant (`LVP_POISON_MEMORY=1`) instead of
zeros, a read of memory never written changes the image. Two such reads found, both in stock's code (0067):

* SSAO's output is never written in its last column at a width of 1920 (also 480, 960, 1680, 3840): the interleave
  pass bounds its invocations with `ivec2(1.0 / pixel_size)`, 1919.9999 in single precision. The opaque pass reads that
  column as it was in memory. Found by clearing the textures created without data at creation, bisected to the SSAO
  output (debug hook `FELLOW_DEBUG_CLEAR_TEXTURES`). SSIL's interleave has the same bound (its output is cleared when
  created: no indirect light in that column).
* The depth mipmaps of SSAO and SSIL at quality high: four invocations of each workgroup store into each texel of the
  third level (a condition compares a value with itself), two of them averaging shared memory past the end of their
  column. On a GPU the store that lands last changes from run to run.

Not a source of the floors, but of lavapipe A/A differences in capture mode: a draw's ubershader and its specialized
pipeline do not give the same alpha-scissor foliage on lavapipe (the roughness limiter's derivatives after `discard`,
0057), so a capture differs between runs when a pipeline finishes compiling around the captured frame. Exactness
checks on lavapipe use the demote diagnostics (`debug/scene_shader_experiments` 4194304) and a settle long enough for
no ubershader draw (`FELLOW_DEBUG_FORCE_UBERSHADER=1` draws everything with the ubershader, for comparisons).

## 5. Synchronization

Vulkan synchronization validation (lavapipe, 480 × 270, captures) with `fellow157`'s defaults, async compute emulated
on a second queue family and cluster LOD on: Lights Lab, PBR Lab, City Lab (SSIL and SSR on), Foliage Lab, Stress Lab
and Blend Lab, 0 hazards.

ThreadSanitizer (a TSAN build of `fellow157`'s code on lavapipe, async compute emulated): City Lab captures and a
short City Lab perf run (the render lists recorded in parallel, the hot workers, the present and submit threads),
Lights Lab captures, a short Foliage Lab perf run (MultiMesh culling). Every report but two is inside lavapipe (concurrent pipeline creation, which Vulkan allows, and
llvmpipe's own threads). The two in the engine are benign: stock's `GLOBAL_GET_CACHED` reads its version outside its
spinlock (the value itself under it), and 0022's persistent instance check reads an entry's valid flag while the thread
that won the frame's rebuild of that entry sets it (the reader either returns, relying on an entry the winner finishes
in the same parallel section, or loses the exchange and returns). Stress Lab does not fit in the container's memory
under TSAN.

## 6. Autotune without ubershader frames in its evaluations (0068, measuring)

An evaluation of a tunable that is a pipeline bit (the shadow pyramids, light tile culling, persistent instance data)
is now prepared: for a few frames the passes request, for each pipeline they bind, the one with that bit the other way,
and the evaluation starts once a frame finds them all compiled. lavapipe, perf runs with a shortened schedule: draws
with the ubershader during the measurement PBR Lab 73 → 13, Lights Lab 10 → 1, City Lab 4 → 1; the evaluation frames
0064 had to leave out 8 → 2 (PBR Lab); 2-9 frames of preparation, no timeout; the same images. What remains are
materials that come into view during an evaluation. (to measure on the RTX: Stress Lab's compilations during the
finals' measurement)

## 7. Tooling

* The bench: `--fellow_frames_keys` chooses the FellowEngine counters recorded per frame (with `--fellow_frames=1`);
  `--max_fps` paces the main thread (diagnostics only: the runner's `--fixed-fps` disables the engine's own cap).
* `.tools/scripts/spikes.py`, `spikes_frames.py`.
* Debug hooks (environment variables of the FellowEngine build, off otherwise): `FELLOW_DEBUG_CLEAR_TEXTURES=a:b`
  (the textures created without data with a creation index in [a, b) cleared when created: bisects a read of memory
  never written, with lavapipe's `LVP_POISON_MEMORY=1`), `FELLOW_DEBUG_TEXTURE_LOG=1`, `FELLOW_DEBUG_FORCE_UBERSHADER=1`.

## 8. Next

(pending)
