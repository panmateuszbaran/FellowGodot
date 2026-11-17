# FellowEngine — milestone 136 report (a second GPU: the laptop's Intel GPU)

Status: **FINAL for its first part** (2026-11-17; the terrain holes' run-to-run differences on Intel open). Branch
`fellow/geometry`. The laptop's integrated GPU (the Core Ultra 9 275HX's "Intel(R) Graphics", Godot's Vulkan device 1,
Intel's Windows driver 32.0.101.6629, Vulkan 1.4.303; it drives the laptop's panel) beside the RTX 5070 Ti Laptop GPU
(jobs f476-f486); lavapipe in the cloud.

## 0. Result

FellowEngine had been tuned and checked on one GPU, an NVIDIA one — the readiness review's largest gap. Its first runs on
a second vendor's GPU and driver:

* **A crash found and fixed** (decision 0261): Intel's driver crashed reading the Vulkan pipeline cache for its
  periodic save while other threads created pipelines — stock Godot 4.7.2 too (5 of 5 runs of a stress made for it; one
  of a scene's first runs). `rendering/fellow/frame/pipeline_cache_exclusive` (on for Intel GPUs) reads the cache only
  while no pipeline is being created; the stress: 10 of 10 runs crashed without it, none of 10 with it.
* **The pictures**: the image regression suite's 38 scenes on Intel against lavapipe's goldens — on average within a
  level of 255 in all but two: the ray-traced reflections (Intel has no `VK_KHR_ray_tracing_position_fetch`:
  FellowEngine's fallback shades the hits a little softer, the same scene) and the destruction scene (its physics on
  Windows breaks the walls slightly otherwise). `dithered_fade_check.gd` passes on Intel with the RTX's and lavapipe's
  numbers (the fades, the hand-overs, the shadows of milestone 135).
* **Run to run**: 30-33 of the 38 scenes the same picture bit for bit on every run; the others within 1 level in at
  most 18 pixels (Godot's own: stock's lights too), and the terrain with holes within 5 levels in about 2 600 pixels
  around its holes — open. Making every draw wait for its pipeline (`rendering/fellow/debug/wait_pipelines`, new, and
  the suite's `--wait-pipelines`) did not change that: not the ubershader's draws. FellowEngine off against on: the
  lights' and materials' scenes 1 level apart in 0.001 % of their pixels on Intel (exact on lavapipe and the RTX).
* **The speed** (720p, the "high" preset, the first 600 frames of each flythrough, 2 runs each): FellowEngine's defaults
  faster than stock in 8 of the 9 scenes of milestone 41's final check — Sponza −49 %, as on the RTX — and City Lab
  7 % slower:

| scene (Intel GPU, 1280 × 720) | stock | FellowEngine | | on the RTX (milestone 41, 1080p) |
|---|---:|---:|---:|---:|
| Sponza | 57.3 ms | 29.3 ms | **−49.0 %** | −54.4 % |
| Bistro exterior | 30.9 ms | 22.6 ms | **−26.9 %** | −37.4 % |
| Bistro interior | 17.0 ms | 13.0 ms | **−23.3 %** | −23.6 % |
| Lights | 12.5 ms | 9.6 ms | **−23.1 %** | −23.1 % |
| Stress | 28.0 ms | 21.6 ms | **−22.6 %** | −1.8 % |
| PBR | 13.7 ms | 10.7 ms | **−21.9 %** | −15.0 % |
| Foliage | 16.0 ms | 13.3 ms | **−17.3 %** | −18.9 % |
| Crowd | 14.6 ms | 13.5 ms | **−7.5 %** | −19.3 % |
| City | 12.6 ms (11.4-13.7) | 13.5 ms | **+7.3 %** | −29.1 % |

## 1. Checks

`bench/pipeline_cache_stress.gd` (new); `tools/fellow_regression/regression.py` on another GPU (`FELLOW_GODOT_ARGS`,
read by `fellow_agent.py`, passes `--gpu-index 1`; `--wait-pipelines`; goldens of its own); `bench/dithered_fade_check.gd`
on Intel (its streamed swap's fade now judged by pixels differing at all: Intel shades the two meshes within 16
levels); the bench runner's `^--gpu-index 1`.

## 2. Next

City Lab on Intel (the passes' GPU times: which of FellowEngine's choices costs there), and the terrain holes' pixels
that differ from run to run on Intel.
