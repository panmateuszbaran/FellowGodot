# FellowEngine — milestone 2 report

Status: **DRAFT** — every per-pass and image result below is final for its session; the frame-level table (§1) comes
from the day-state session `fellow-m2_perf` (running).

Base: Godot **4.7.2-stable** · Branch `fellow/geometry` (contains `fellow/shadows`) · Measured binary `fellow16`
(`0ba5e35`) · Machine: Acer Predator PHN16S-71, Core Ultra 9 275HX, RTX 5070 Ti Laptop 12 GB, Windows 11, Vulkan.

Decision records: `docs/decisions/0001` (TAA, milestone-2 follow-up), `0004` (directional shadow cache), `0005`
(shadow sampling), `0006` (temporal SSAO, parked, branch `fellow/ssao`), `0007` (mesh clusters).

## 1. Result

Milestone 1 named directional shadows the largest measured pass of the geometry-heavy scene and listed opaque
shading, alpha-blended foliage and the TAA foliage regression as the next targets. Milestone 2 addressed the shadow
maps, the shadow lookups, the geometry submitted by large meshes, and the TAA regression; every change is exact
(image-identical within the GPU's run-to-run variation) except the TAA change, which is measured against the ground
truth, and two optional look changes that stay off by default.

| Change | Decision | Effect (per-pass GPU timestamps, 1080p) |
|---|---|---|
| Directional shadow static cache (0004) | KEEP | Sponza shadow pass 3.40 → 0.28 ms; Bistro exterior 0.52 → 0.26 ms; render-thread CPU −42 % |
| Shadow sampling skips: N·L = 0 pixels, unblended cascade (0005) | KEEP | opaque pass −0.07 to −0.29 ms |
| Mesh clusters: frustum culling inside large surfaces (0007) | KEEP | Sponza depth pre-pass −43 %, opaque −22 % (−1.2 ms GPU) |
| TAA v2 motion soften (0001 follow-up) | KEEP | foliage regression fixed; ≥ stock on 8/8 sequences |
| PCSS blend band, parallel-light penumbra (0005) | optional (look) | opaque −0.18 to −0.40 ms (band); penumbra fixes dissolving cascade-0 shadows |
| Max-pyramid early-out (0005) | REJECT (v1) | exact, no net gain; v2 with a fully-shadowed early-out under test |
| Temporal SSAO (0006) | REJECT / parked | no iso-quality saving |

Sum of timestamped passes, stock → FellowEngine configuration (night profiles, relative): Sponza 11.8 → 7.2 ms
(−39 %), Bistro exterior 5.0 → 4.4 ms (−12 %), Bistro interior 3.7 → 3.6 ms (−3 %), PBR Lab 2.4 → 2.3 ms (within
noise).

### Frame level, day state (`fellow-m2_perf`, 1920×1080, 3 runs per variant, interleaved)

*pending (session running).*

## 2. The four questions

*pending the day-state session.*

## 3. Per-pass evidence (night profiles; ±10 % machine noise, relative per-pass deltas reliable)

* Shadows: `benchmarks/results/m2_profile7_table.md` (stock / cache / cache without gate / sampling skips / blend band /
  cache + skips) and `m2_mm_profile_table.md` (max pyramid).
* Geometry: `benchmarks/results/m3_cl_profile_table.md` (stock / clusters / milestone-2 shadows / both) and
  `m3_geo_sponza_table.md`, `m3_geo_bistro_table.md` (stock with coarser LOD thresholds and without scene parts —
  the depth pre-pass, the opaque pass and the alpha-blended foliage are geometry-bound in Sponza).

## 4. Image verification

| Comparison | Result |
|---|---|
| stock vs shadow cache (`m2_verify7`, 60-frame sequences, pre-roll 200) | 67.8-94.6 dB, ≤ 0.003 % pixels > 2/255 |
| stock vs cache + sampling skips | 67.8-94.7 dB, ≤ 0.003 % |
| FellowEngine without vs with mesh clusters (`m3_cl_temporal`) | Sponza 80.4 dB, Bistro ext. 101.2, Bistro int. 69.3, max diff ≤ 5/255 (PBR null test: 67.8-82.1 dB) |
| mesh clusters, every variant on lavapipe (stress setting: every surface ≥ 256 triangles in 64-triangle clusters) | 0 differing pixels |
| PCSS blend band (`m2_verify4`) | 36.3-56.1 dB, up to 5.7 % pixels (look change) |
| TAA motion soften vs ground truth (`m2_taa_sweep3`) | ≥ stock in PSNR and SSIM on 8/8 sequences; mean PSNR stock 34.00, v2 35.00, chosen 35.05 dB |

Fixed cameras against the ground truth (`fellow-m2_capture_gt`) and motion sequences (`fellow-m2_temporal`): *pending
analysis.*

## 5. Mistakes and dead ends (kept visible)

* Cache lattice first derived independently of stock's snapping: 47-51 dB vs stock → re-derived from stock's exact
  snapping; irregular frames fall back.
* Thrash guard counted stock frames as misses (cache stuck off) and a 2 m LOD-travel invalidation forced constant
  rebuilds; fixed (texel LOD, guard ignores stock frames).
* A run was killed as "hung" after 30 s because elapsed time was misjudged (the Windows clock is now the reference).
* A bundle reused a staged file name and delivered stale content to Windows; bundles now always get new names.
* Night state (display off, session locked): ~490 ms stalls every ~1.03 s and GPU clocks jumping 770-1770 MHz; per-run
  GPU totals vary by up to ±10 %. Night numbers are used only for relative per-pass comparisons.
* The second version of the cost gate was motivated by a Bistro interior +7.4 % that was noise; the change itself
  stands on its own.
* The PCSS blend band seemed to lose a shadow near the camera in Bistro; the cause was stock's point-light PCSS
  formula with blockers pancaked onto the first cascade's near plane (decision 0005), not the band.
* Max pyramid v1 was not exact at first on lavapipe: one compute list for all levels (layout transitions only happen
  between lists) and float block math; both fixed, then exact — and rejected for no net gain.
* TAA: a parameter sweep (anti-flicker, clip γ, feedback) could not fix the foliage regression; velocity dilation off
  made it worse.
* Mesh clusters v1 kept the original index buffers and added reordered copies: +446 MB of VRAM in Sponza; replaced by
  in-place reordering decided at the first set-up. Its synchronous build made Sponza's load 2.8 s longer; moved to
  worker threads.
* An offline estimate of stock's LOD (position-only simplification) predicted far fewer triangles than the engine
  actually submits; the engine's own counters are the reference.
* The temporal SSAO experiment found no iso-quality saving (decision 0006).

## 6. Exact files changed

*to be listed from `git diff` at the final commit.*

## 7. Remaining high-value opportunities (measured)

1. **Geometry** still dominates Sponza after clusters (depth pre-pass 0.87 ms, opaque 2.31 ms of which ~0.6 ms
   geometry, transparent pass 1.66 ms of which ~0.9 ms is the alpha-blended cypress leaves). Offline estimates:
   occlusion (two-phase depth pyramid on the GPU) −24 % of the remaining camera-pass triangles on average, −65 % in the
   galleries; per-cluster LOD with locked borders at stock's 1-px rule about −25 %.
2. **PCSS lookup**: still about a third of the opaque pass where the sun shadows a large area (Bistro exterior).
3. **SSAO + SSIL + SSR**: 1.0-1.2 ms in every scene.
