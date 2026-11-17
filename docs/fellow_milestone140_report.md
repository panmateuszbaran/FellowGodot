# FellowEngine — milestone 140 report (far characters animate less often)

Status: **FINAL** (2026-11-17). Branch `fellow/geometry`. The cloud's CPU (headless) and lavapipe (the laptop's GPUs
busy).

## 0. Result

* **Animation update rate by distance** (decision 0265, opt-in `rendering/fellow/animation/update_lod`): an
  AnimationPlayer or AnimationTree processed every frame, its root a Node3D and without a root motion track, evaluates
  every frame within `update_lod_distance` (20 m, for a 75° view) of its camera, every 2nd frame within twice that,
  every 4th within four times, up to every 8th, and every 8th out of view — each on its own frames, a skipped frame's
  time added to its next evaluation (the clips on time, their method tracks fired then). A skipped frame evaluates no
  track, poses no skeleton and hands no skin to the renderer.
* Crowd Lab's 256 characters playing as a game plays them, on a field 120 m square seen from its edge: **50
  evaluations a frame instead of 256, the main thread's process step 3.4 ms → 1.2 ms (−66 %)**; the busiest frame 52
  evaluations (spread, no spikes); every evaluation's clip at its time without the setting. A field of 40 m −39 %, of
  240 m −70 %.
* The near characters' pictures unchanged; the far ones in their last evaluated pose (at eye level only characters
  beyond 20 m differ). Off by default: the image regression suite exact, manual seeks unchanged.

## 1. Checks

`bench/animation_lod_check.gd` (new: evaluations by distance band, their clips' times, the main thread's process
step); `bench/animation_check.gd` (Crowd Lab's manual seeks: the same hashes off and on); Crowd Lab `--crowd_process=1`
(the players in the idle process) on lavapipe's cameras; the image regression suite (exact).

## 2. Next

Crowd Lab with `update_lod` on the RTX (the render thread's and the GPU's skinning saved too), Bistro streamed,
Frontier Lab's levels of groups, City Lab on the Intel GPU — when the laptop's GPUs are free.
