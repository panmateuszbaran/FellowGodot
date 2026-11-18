# FellowEngine — milestone 145 report (Bistro streamed: the longer rare frames, and a hang)

Status: **FINAL** (2026-11-18). Branch `fellow/geometry`. The RTX (jobs f496-f501, m142 and m145a) and the Intel GPU
(f497); lavapipe in the cloud.

## 0. Result

* **Why Bistro's streamed frames had longer rare ones** (decision 0269; milestone 143: p99 5.3 → 7.4 ms): its textures'
  churn. In a 30-second flight its 320 streamed textures were loaded 4 800 times and released 2 200 times — released
  two seconds after the camera stopped needing their mips, needed again a few seconds later — and every swap makes the
  uniform sets of the materials using the texture again (5 600 a run; counted now, `material_uniform_sets_created`).
  Releases held 10 s instead of 2 s: 1 400 loads and 360 releases, the 99th percentile **7.3 → 5.9 ms**, the render
  thread's 3.6 → 2.7 ms, the frame 4.34 → 4.24 ms, for 50 MB more video memory (1 670 MB at its peak, against 3 080 MB
  whole). Not the swaps' bursts (capped at 1 or 2 a frame: the same; the cap removed) nor the evaluation (halved: the
  same).
* **A rare hang** (open): three of 28 runs held 10 s hung — the main thread waiting for a frame's fence the GPU never
  signalled, the GPU idle, the present thread idle; none of 27 held 2 s. The default stays 2 s until it is understood.
  Found with FellowEngine's hang reports (their minidumps read by `benchmarks/tools/crash/minidump_stacks.py`, new:
  every thread's call chain, named from the linker map).
* **The terrain holes on Intel** (milestones 136, 143): their pictures differ from frame to frame within one run too
  (frames 60, 90 and 120 of a still view: up to 4 levels) with FellowEngine on, and with it off are the same in every
  frame of every run — bit for bit on Intel. Something of FellowEngine's that changes from frame to frame in the
  patches over the holes, where its frame-to-frame state is not that of the rest of the terrain.

## 1. Checks

Bistro exterior streamed on the RTX: `m145_f496` (the hitch sampler), `f498` (swaps capped), `f499` (evaluation,
hold), `f500`-`f501` (hangs; hang reports on); Streaming Lab on lavapipe with each hold and under Vulkan's core and
synchronization validation (no message); `th_frames.py` on Intel (f497); the image regression suite (exact).

## 2. Next

The hang: the stalled frame's submission's waits and where each comes from, reported when its fence is late — then the
fix, and releases held 10 s.
