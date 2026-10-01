# FellowEngine decision records

Every change to engine behaviour or performance goes through a decision gate before it can be enabled by default.
One file per experiment, numbered in the order they were started. A record is written when the experiment is
measured, not when it is proposed, and it is never rewritten to hide a result: later re-measurements are appended.

## Gate rules

* The change must be isolated behind a runtime toggle under `rendering/fellow/*` (default **off** until KEEP), so
  stock behaviour and the experiment run from the same binary.
* Evidence must come from the benchmark suite (`benchmarks/`), interleaved ABBA runs on the same machine, with the
  settings recorded next to the results. Anything measured on another machine or driver is labelled as such.
* Performance and quality are judged together. A speed-up that changes the image must say how (diff images, PSNR/
  SSIM/LPIPS vs. the accumulated ground truth, temporal metrics); an image change that costs time must say how much.
* Anti-cheating: no lowered internal resolution, sample counts, shadow resolution, LOD, draw distance or effect
  quality unless that *is* the experiment and it is reported as a quality trade-off (iso-performance comparison).
* Decisions: **KEEP** (may be enabled by default), **REWORK** (promising, not acceptable as is), **REJECT**
  (removed or left off; the record stays).

## Template

```
# NNNN — <title>

Status: KEEP | REWORK | REJECT | MEASURING      Branch / commit: ...      Toggle: rendering/fellow/...

## Problem
## Evidence (before the change)
## Stock implementation
## Proposed implementation
## Expected benefit
## Actual performance        (tables: scene, resolution, stock vs fellow, delta, CV, runs)
## Actual visual quality     (captures, diffs, metrics vs. ground truth, temporal metrics)
## Regression risk
## Decision
```

## Index

| # | Experiment | Toggle | Decision |
|---|---|---|---|
| [0001](0001-taa-resolve-v2.md) | TAA resolve v2 (reverse-Z dilation, YCoCg variance clip, no clip collapse) | `postfx/taa_resolve_v2` | see record |
| [0002](0002-taa-history-pingpong.md) | TAA history ping-pong (no history/velocity copies) | `postfx/taa_history_pingpong` | see record |
| [0003](0003-gpu-shallow-water.md) | GPU shallow-water prototype (`FellowShallowWater3D`) | `fluid/enabled` (new feature) | see record |
| [0004](0004-directional-shadow-cache.md) | Directional shadow static cache (guard-banded per-cascade cache, depth re-based copy, dynamic casters on top) | `shadows/directional_cache` | KEEP |
| [0005](0005-directional-shadow-sampling.md) | Directional/positional shadow sampling: N·L skips, unblended cascade skip (exact); PCSS blend band, parallel-light penumbra (look); max-pyramid early-out | `shadows/skip_*`, `shadows/pcss_*`, `shadows/directional_max_mip` | skips KEEP; look changes optional; max pyramid REJECT |
| 0006 (branch `fellow/ssao`) | Temporal SSAO (rotated pattern + accumulation) | `ssao/*` | REJECT / parked |
| [0007](0007-mesh-clusters.md) | Mesh clusters: frustum culling of k-d clusters inside large static surfaces | `geometry/mesh_clusters` | KEEP |
| [0008](0008-light-cluster-depth.md) | Light clusters with logarithmic depth slices and conservative per-element depth ranges | `lights/cluster_depth_slices` | pending |
