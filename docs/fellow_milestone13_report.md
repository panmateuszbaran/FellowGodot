# FellowEngine — milestone 13 report (frame pacing, LOD of large surfaces, run-to-run floors)

Status: **DRAFT** (`fellow152`-). Branch `fellow/geometry`. Benchmark machine as before (Acer Predator PHN16S-71, Core
Ultra 9 275HX, RTX 5070 Ti Laptop GPU, NVIDIA GPU only, Windows 11, 1920×1080, "high" preset). Decision records
`docs/decisions/0063`-; user-facing summary `docs/FELLOWENGINE_OPTIONS.md`.

## 0. Result

(pending)

## 1. The question

Milestone 12's final measured the frame rate of every scene where milestone 11 had left it and found what averages
hide: City Lab's runs with FellowEngine have had frames of 16-39 ms since milestone 10 (1 % low 54-197 fps against
stock's 214-239), with the GPU and the render thread idle-normal in those frames. This milestone asks where those frames
go, what the run-to-run floors that limit exactness checks are made of, and measures the per-cluster LOD started in
milestone 12 (0063).

## 2. Large surfaces at the LOD of their parts (0063, KEEP as an option)

(see the decision record: Sponza GPU −3.1 %, render thread +0.21 ms, 63.6 dB on a moving sequence; no effect elsewhere)

## 3. City Lab's spikes

`m13_f185_present`, four runs each (City Lab): every FellowEngine configuration has them in most runs — with the present
and submit threads (37-57 frames over 6 ms per run), presenting on the render thread (2-129), with the present thread
and without the submit thread (0-143) — and stock in none. `m13_f186_spikes`: also without async compute, with four
swap chain images, with a frame queue of three; not with FellowEngine disabled at run time (0-1). Runs whose frames are
limited by the GPU (the render thread waits on fences for 1.3-2.1 s of the run) have none; runs limited by the CPU
(fence waits 0.06-0.17 s) have them. Per frame (`m13_f186_perframe`): every spike is one `vkQueuePresentKHR` call of
6-45 ms (mean 0.42-0.46 ms per frame against 0.1 ms with FellowEngine disabled), which the render thread then waits for.

(in progress: `m13_f188` — the frame rate capped at stock's 330 fps and at 420 fps, the render thread's parallel work
off, hot workers not spinning, no parallel replay; `m13_f181` — with 12 processes spinning beside it)

## 4. The run-to-run floors

`m13_f180_floor`, warm 60-frame sequences, two runs of one build against each other:

| scene | stock A/A | FellowEngine A/A | FellowEngine without async compute A/A |
|---|---|---|---|
| Bistro interior | 69.2 dB (max \|d\| 2) | 67.1 dB (21) | 67.1 dB (13) |
| PBR Lab | 82.1 dB (3) | 73.5 dB (17) | 73.5 dB (28) |
| Sponza | 80.3 dB (1) | 75.5 dB (4) | 75.5 dB (4) |
| Lights Lab | 98.4 dB (1) | 101.8 dB (1) | 103.1 dB (1) |

Stock is not bit for bit between runs either; FellowEngine's floors are lower and not from async compute. (in progress:
`m13_f187` — autotune off)

## 5. Tooling

* The bench: `--fellow_frames_keys` chooses the FellowEngine counters recorded per frame (with `--fellow_frames=1`);
  `--max_fps` paces the main thread (diagnostics only: the runner's `--fixed-fps` disables the engine's own cap).
* `.tools/scripts/spikes.py`, `spikes_frames.py`.

## 6. Next

(pending)
