# FellowEngine — milestone 5 report (scene-dependent choices, persistent instance data)

Status: **FINAL for `fellow71`/`fellow74`** (measured 2026-10-07). Branch `fellow/geometry`. Benchmark machine as in
milestone 4: Acer Predator PHN16S-71, Intel Core Ultra 9 275HX, RTX 5070 Ti Laptop GPU (driver 617.14), Windows 11,
1920×1080, the "high" preset. Decision records: `docs/decisions/0015` (async compute, updated), `0020` (autotune),
`0021` (instance buffers in system memory, rejected), `0022` (persistent instance data), `0023` (TAA in 16 × 16 groups,
rejected), `0024` (SSAO/SSIL gathers by rotation, rejected), `0025` (auto exposure's reduction).

## 0. Result

New defaults from `fellow71`: **persistent instance data** (0022), **autotune** (0020) and **async compute on NVIDIA
GPUs** (0015); from `fellow74` also the **two-barrier luminance reduction** (0025). Against milestone 4's defaults (all
three off), between processes (`m5_f73_defaults`, `fellow71`, 3 ABBA runs of 20 s each; frame = wall time per frame):

| scene | milestone 4 defaults | new defaults | Δ frame | pairs faster | Δ GPU time |
|---|---:|---:|---:|---:|---:|
| City Lab | 2.396 ms | **2.299 ms** | **−4.1 %** (p 0.03) | 3/3 | +3.4 % (n.s.) |
| City Lab, draw-heavy | 2.624 ms | **2.504 ms** | **−4.6 %** (p 0.005) | 3/3 | |
| PBR Lab | 2.883 ms | **2.766 ms** | **−4.0 %** (p 0.000) | 3/3 | −2.1 % |
| Lights Lab | 2.989 ms | **2.943 ms** | **−1.5 %** (p 0.02) | 3/3 | −1.4 % |
| Bistro exterior | 5.087 ms | 5.049 ms | −0.7 % (p 0.17) | 3/3 | −0.8 % |
| Bistro interior | 4.092 ms | 4.100 ms | +0.2 % (n.s.) | 0/3 | +0.3 % |
| Sponza | 7.355 ms | 7.374 ms | +0.2 % (n.s.) | 1/2 | +0.2 % |

Plus the luminance reduction (0025, in one process, `m5_f73_lum_ab`): Bistro exterior −0.7 %, Bistro interior −0.8 %,
Sponza −0.4 % (the auto exposure pass 43 % faster; only scenes with auto exposure). The render thread gains more than the
frames show: persistent instance data alone takes 13.5 % off City Lab's render thread (`m5_f70_city_pi_ab`), but on this
laptop City Lab runs at the GPU's limit plus the hybrid-graphics presentation (milestone 4, §9).

Every change is exact: the same pixels whichever way each is set (lavapipe: 0 differing pixels; RTX: bit-identical in
Bistro exterior and City Lab, at the run-to-run floor elsewhere), `cpu/verify` 0 mismatches, the luminance reduction
compared bit for bit on the GPU (0 mismatches over 44 million texels). Three experiments were measured and rejected
(0021, 0023, 0024; §5).

One run in about 80 with the new code (Sponza, `m5_f73_defaults`) ended in an access violation without any output: the
engine's crash handler covers only the main thread, and only in debug builds. Windows builds now report a crash in any
thread (exception, address, thread, stack as module + offsets; the build keeps the linker's map). 20 more runs of 60 s
(`m5_f75_crash_*`: Sponza ×12, Bistro exterior ×4, City Lab ×4) did not crash again: open, watched in every run.

## 1. Why this milestone

Milestone 4 ended with two findings (its §9, §12). Several of FellowEngine's exact optimizations pay in one scene and
cost in another (the directional shadow pyramid saves Sponza 14 % and costs City Lab 2.7 %; async compute gains 1-2 %
where the GPU is the limit and costs 3 % where the render thread is), so no static default is right everywhere. And
the render thread still wrote every list element's whole instance data (176 bytes) into GPU memory over PCIe every
frame, although almost none of it changes between frames.

## 2. Autotune (decision 0020)

Exact optimizations change no pixel whichever way they are set, so the render thread can run each one on and off in
alternating blocks of frames, compare the frames' GPU time (timestamps around the frame's viewports) or, for those that
move work between the render thread and the GPU, the frames' wall time, and keep the faster choice. Tunables: the
directional and positional shadow pyramids (GPU time), async compute, the frame plan and persistent instance data
(wall time). Evaluations: 6 pairs of 16-frame blocks in the order on, off, off, on…, decided at 2.5 standard errors
and 0.5 % of the frame; ended after 3 pairs when the difference is clear (3 % and 4 standard errors).

What the measurements changed on the way:

* `fellow61` (pyramids only): City Lab −4.4 %, draw-heavy −2.9 % (the pyramid switched off), Sponza kept.
* Re-evaluation when the frame time changes by 20 % or after 30 s (`6ea366a`) made Sponza's camera path trigger 27-28
  re-evaluations per run, every tunable each time (`m5_f69_gpu_cand`: +1.2 %). Since `fellow68` a change-triggered
  re-evaluation backs off (spacing doubled while it keeps the choice) and evaluation frames are left out of the
  frame-time average.
* `fellow68` was still +2.9 % in Sponza against the same settings without autotune (`m5_f70_gpu_cand`): 24-26
  evaluations per run took about 60 % of the frames, half of them with a tunable off. Since `fellow70` all evaluations
  together take at most 20 % of the frames (`autotune/max_duty_percent`).
* A tunable measured by wall time in a noisy CPU-bound frame (async compute in the draw-heavy City Lab, ~3 %) was decided
  differently from run to run (async compute in 2028, 6282 and 10 195 of 10 868 frames). Since `fellow70` each
  evaluation's pairs are pooled with the earlier ones in the same conditions (up to 36 pairs).

## 3. Persistent instance data (decision 0022)

Each surface cache gets an entry (slot) in a buffer kept from frame to frame, holding stock's instance data without the
two per-pass fields; the per-frame buffers hold one 16-byte element per list element (slot, flags, GI offset). An entry
is rebuilt only when its instance's data version changed (every setter of a field it holds, and the frame's update of
the previous transform), compared with a CPU copy, and rewritten only if it changed. The scene shaders read the element
and then the entry (a specialization bit; pipelines without it are unchanged).

| version | how entries reach the GPU | City Lab render thread | GPU time | draw-heavy render thread | GPU time |
|---|---|---:|---:|---:|---:|
| v1 (`fellow66`) | per-frame exchange on every surface; a scatter dispatch per list | +1.3 % | +0.9 % | +2.1 % | +1.3 % |
| v2 (`fellow67`) | read-only check; one scatter dispatch per frame | −6.0 % | +1.3 % | −7.2 % | +1.5 % |
| v3 (`fellow68`) | written by the CPU into the frame's region of a dynamic persistent buffer | **−13.5 %** | +0.4 % | −4.1 % | +0.9 % |

(In one process, blocks of 30 frames; `m4_f68_*_pi_ab`, `m5_f69_*_pi_ab`, `m5_f70_*_pi_ab`.) v3: the buffer has one
region per frame in flight; the frame's first list fill maps the next region and writes into it the entries written
since that region was last mapped; a changed entry is written straight into the frame's region by the list element that
found it. No upload, no dispatch, no barrier. What the GPU still pays is the slot's indirection in the vertex shaders
(depth pre-pass +0.004 to +0.017 ms). Persistent instance data is also an autotune tunable: it pays where the render
thread is the limit.

Verification: `cpu/verify` reads each element's entry from the frame's region, as the GPU does, and compares it with
stock's data: 0 mismatches on RTX (City Lab, Lights, Stress temporal; City, PBR, Bistro exterior captures; also with
autotune switching it on and off every few frames) and on lavapipe; images bit-identical in Bistro exterior and at the
floor elsewhere (RTX), 0 differing pixels on lavapipe.

## 4. Async compute in CPU-bound frames (decision 0015)

Two changes: uploads (transfer workers) are left to the main queue, so the async part runs in Sponza (before, 1.6 % of
its frames used it), and autotune decides per scene. Alone it gains 1-2 % in the GPU-bound scenes and costs ~3 % in the
draw-heavy City Lab, which autotune now avoids. On by default on NVIDIA GPUs (the setting is 0 off, 1 on, 2 NVIDIA, the
default): concurrent sharing of every image is fixed at start-up, and other vendors were not measured.

## 5. Rejected

* **Instance data buffers in system memory (0021).** The render thread writes twice as fast (−4 to −7 %), but the GPU
  reads the data over PCIe in every pass that draws (+2.5 to +3.4 % GPU time). Led to 0022.
* **TAA resolve in 16 × 16 groups (0023).** Fewer tile loads per pixel, but the pass got 0.006-0.021 ms slower.
* **SSAO/SSIL gathers by rotation (0024).** Each workgroup took the pixels of one sample rotation so its taps would be
  coherent; the SSAO gather got 0.016-0.066 ms slower (SSIL's 0.006 ms faster).

## 5b. Auto exposure's reduction (decision 0025)

The luminance reduction summed each workgroup's 64 values by a tree of six steps with a barrier after each. Eight threads
now compute what the tree holds at indices 0-7 after its first three steps, and one thread the last three: the same
additions on the same operands, two barriers instead of seven. The pass went from 0.106 to 0.060 ms in Bistro and
Sponza. Auto exposure adapts by the frame's time step, so images of two runs differ even for the same build (by 1/255 in
tens of thousands of pixels on lavapipe); the reduction itself was compared on the GPU (§8).

## 6. Where the GPU time goes now (`m5_f69_sizing`)

One feature removed per variant (1 run each, `fellow67`), GPU ms saved per frame:

| scene | GPU ms | SSAO | TAA | glow | SSIL | directional soft → hard | positional soft → hard | SSR | volumetric fog |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| Bistro exterior | 4.72 | 0.50 | 0.41 | 0.18 | 0.32 | 0.30 | 0.09 | 0.17 | 0.08 |
| Sponza | 7.12 | 0.54 | 0.63 | 0.15 | 0.40 | 0.16 | 0.30 | 0.34 | 0.12 |
| Bistro interior | 3.80 | 0.62 | 0.39 | 0.23 | 0.49 | 0.02 | 0.48 | 0.28 | 0.05 |
| Lights Lab | 2.67 | 0.68 | 0.29 | 0.22 | — | 0.01 | 0.15 | — | 0.20 |
| PBR Lab | 2.53 | 0.72 | 0.38 | 0.25 | — | 0.23 | 0.12 | 0.03 | 0.01 |
| City Lab | 1.79 | 0.62 | 0.30 | 0.21 | — | 0.06 | 0.01 | — | — |

SSAO (high quality, full size in the benchmark preset) is the most expensive effect everywhere (25-35 % of the GPU frame
in the labs and City Lab, 8-16 % in Sponza and Bistro), then TAA and SSIL. Exact work on SSAO so far found nothing (0011: shared tiles; 0024:
coherent taps): the gather is limited by neither its centre/neighbour loads nor the spread of its taps.

## 7. Shortened benchmark protocol

From this milestone (at the user's request): in-process A/B first (blocks of 30 frames, every variant in every block of
the path; per-pass GPU and CPU times with `--profile`), between-process ABBA only for candidate defaults, with 20-second
measurements, 5-second warm-up and a 30-second heat soak (`--warmup 5 --measure 20 --heat-soak 30`), 3 runs. A job
went from ~3 hours to ~1 hour; the next job is built and verified on lavapipe while the current one runs.

## 8. Verification

* `cpu/verify` (stock's lists and instance data recomputed and compared; for persistent instance data each element's
  entry read from the frame's region as the GPU reads it) on RTX: 0 mismatches in City Lab, Lights, Stress temporal and
  City, PBR, Bistro exterior captures, with async compute and with autotune switching persistent instance data and the
  other tunables every few frames (`m5_f70_verify_*`, `m5_f73_verify_*`: the new defaults); 0 async conflicts.
* Identity, milestone 4's defaults vs the new ones (`m5_f73_temporal`, 60-frame sequences): Bistro exterior and City Lab
  bit-identical, Lights Lab and Sponza at their run-to-run floor (max |d| 2 and 3, no pixel above 2).
* lavapipe: persistent instance data off vs on with `cpu/verify` (City Lab walk, Stress Lab temporal; also with
  autotune switching it on and off), the coherent gather (City Lab, Lights Lab with ultra SSAO/SSIL, PBR Lab), the
  luminance reduction (PBR Lab with auto exposure and a fixed frame step), the new defaults against the old: 0 differing
  pixels, 0 mismatches; with the Khronos validation layer (async compute, persistent instance data, autotune): no
  message.
* The luminance reduction on RTX: `postfx/verify_luminance_reduce` runs both reductions on the same inputs every frame
  and compares every level bit for bit: 0 mismatches in 44 298 789 texels (Bistro exterior, Bistro interior, Sponza).

## 9. Measurement notes

* GPU clocks vary by ±3-5 % between runs on this laptop (1520-1720 MHz in Sponza, 1950-2140 MHz in City Lab; logged per
  run): between-process differences of 1-3 % in GPU-bound scenes need the paired ABBA statistics, and in-process A/B
  is what sizes per-pass effects.
* In `m5_f70` the GPU ran slower than in `m5_f69` (City Lab 2.22 against 1.83 ms of GPU time in the in-process runs):
  both City Lab variants were then bound by the GPU, which hides render-thread savings in their frame times.

## 10. Next

* **GPU occlusion culling (two-phase).** The remaining GPU time is mostly the passes that draw geometry (Sponza: depth
  pre-pass 0.98 ms, opaque 2.08 ms, transparent 1.27 ms; Bistro exterior: 0.53 and 1.81 ms) and SSAO. Drawing first
  what was visible in the previous frame, building a depth pyramid from it, testing everything else against it and
  drawing what is newly visible removes the vertex work of hidden geometry (and the fragment work of hidden alpha-tested
  foliage, which early depth testing does not cull). Conservative; the per-surface slots of persistent instance data
  give stable identities for the visibility history. Not bit-exact where coplanar surfaces tie in depth (the drawing
  order changes): images expected at the floor, measured as such.
* **The open crash** (§0): watched in every run with the new crash report.
* Smaller exact steps between benchmark jobs: SSAO's blur and interleave passes, the glow chain, the copy of the final
  framebuffer for SSIL/SSR.
