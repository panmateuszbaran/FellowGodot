# FellowEngine — milestone 2 report (draft)

Status: **DRAFT** — the per-pass and image results below are final for their sessions; the frame-level performance
table needs the day-state session (`fellow-m2_perf`, laptop unlocked with the display on) and is marked *pending*.

Base: Godot **4.7.2-stable** · Branch `fellow/shadows` · Machine: Acer Predator PHN16S-71, Core Ultra 9 275HX,
RTX 5070 Ti Laptop 12 GB, Windows 11, Vulkan.

## 1. Result

Milestone 2 targeted the largest measured pass of the geometry-heavy scene: directional shadows (Sponza: 28-39 % of
the frame in milestone 1).

* **Directional shadow static cache** (decision 0004): static casters of every cascade are rendered into a
  guard-banded cache and copied (depth re-based to the stock projection) into the atlas each frame; only dynamic
  casters are drawn. Sponza shadow pass **3.40 → 0.28 ms** (whole-frame GPU −27 %), Bistro exterior **0.52 → 0.26 ms**,
  render-thread CPU −42 % in both. Output equals stock within run-to-run noise (67-95 dB, ≤ 0.005 % of pixels differ by
  more than 2/255 over 60-frame sequences). A cost gate keeps light scenes (PBR Lab) on the stock path, where the copy
  would cost more than the casters it saves.
* **Shadow sampling** (decision 0005): skipping the shadow lookup where N·L makes the light's contribution zero, and
  the second PCF cascade where its blend weight is zero — image-identical; opaque pass −0.07 to −0.29 ms (night-state
  profile, to be confirmed in the day state).
* **Look changes, off by default**: PCSS blend band (blend cascades over the last 10 % of a split, like PCF; opaque −0.18
  to −0.40 ms) and a parallel-light PCSS penumbra (decision 0005; stock's point-light formula dissolves the shadows of
  casters beyond the pancake distance in the first cascade).
* **TAA foliage regression of v2** (milestone 1) fixed (decision 0001, follow-up): a parameter sweep (anti-flicker,
  clip γ, feedback) could not fix it; a targeted *motion soften* (pull the clipped history towards the 3×3 mean under
  motion where the neighbourhood's relative contrast is high) does. With soften 0.35 / contrast 0.45 the resolve is at
  or above stock in PSNR and SSIM on all 8 temporal sequences (foliage 31.50 → 32.17 dB; v2 alone 31.11) with the best
  mean PSNR of all variants (stock 34.00, v2 35.00, chosen 35.05 dB).
* **Mesh clusters** (decision 0007, geometry): large static surfaces are split into k-d clusters at load and only
  clusters intersecting the view volume are drawn in the depth pre-pass, the opaque pass and shadow passes — exact
  (a triangle outside the frustum covers no pixel). Sponza: stock submits 8.7 M triangles per frame for the camera,
  4.3 M of which can reach a pixel; 4096-triangle clusters cut the submission to 4.65 M (offline estimate).

## 2. Per-pass evidence (GPU timestamps, 1920×1080, `m2_profile7`, night state — relative only)

(see `benchmarks/results/m2_profile7_table.md`; stock / cache / cache without gate / sampling skips / band / cache+skips)

## 3. Image verification

| Comparison (60-frame sequences, preroll 200) | PSNR mean (min) | pixels > 2/255 |
|---|---|---|
| stock vs cache (`m2_verify7`) | 67.8-94.6 dB (65.5) | ≤ 0.003 % |
| stock vs cache + sampling skips | 67.8-94.7 dB (65.6) | ≤ 0.003 % |
| stock vs PCSS blend band (`m2_verify4`) | 36.3-56.1 dB | up to 5.7 % (look change) |

## 4. Frame-level performance

*pending day-state session.*

## 5. Mistakes and dead ends (kept visible)

* Cache lattice first derived independently of stock's snapping: 47-51 dB vs stock → re-derived from stock's exact
  snapping; irregular frames fall back.
* Thrash guard counted stock frames as misses (cache stuck off) and a 2 m LOD-travel invalidation forced constant
  rebuilds: the first PBR performance session showed the cache never active; fixed (texel LOD, guard ignores stock
  frames).
* A run was killed as "hung" after 30 s because elapsed time was misjudged (the Windows clock is now the reference).
* A bundle reused a staged file name and delivered stale content to Windows; bundles now always get new names.
* Night state (display off, session locked): ~490 ms stalls every ~1.03 s and GPU clocks jumping 770-1770 MHz;
  per-run GPU totals vary by up to ±10 % (a variant identical to stock — the cache in Bistro interior, which has no
  directional shadow pass — measured +7.4 %). Night numbers are used only for relative per-pass comparisons.
* The second version of the cost gate (`dc52a21`) was motivated by that Bistro interior +7.4 %, which was noise; the
  change itself (estimating stock's tight-culled caster set instead of the whole cache volume) stands on its own.
* The PCSS blend band seemed to lose a shadow near the camera in Bistro. Stock without blend splits loses it too
  (34.5 dB vs stock); the tighter caster culling was ruled out with a diagnostic toggle (72.5 dB, no visible change);
  suspected cause: stock's point-light PCSS formula with blockers pancaked onto the first cascade's near plane
  (decision 0005; verification running).

## 6. The four questions

*pending final sessions.*
