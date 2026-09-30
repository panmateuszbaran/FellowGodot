# 0002 — TAA history ping-pong

Status: **KEEP** (enable by default in FellowEngine presets; remains a toggle) · Branch `fellow/postfx` (`c8a1995f0d`) · Toggle `rendering/fellow/postfx/taa_history_pingpong`
(default **off**) · Binary: FellowEngine `7f257b56ff`

## Problem

After the TAA resolve, stock Godot performs three full-resolution copies every frame
(`servers/rendering/renderer_rd/effects/taa.cpp`, `TAA::process`):

1. `temp → internal colour` (RGBA16F) — the resolve cannot write in place because neighbouring work groups read the
   colour tile,
2. `internal colour → history` (RGBA16F),
3. `velocity → previous velocity` (RG16F, with a *target-size* rect on internal-size textures).

## Evidence (before the change)

The stock `TAA` timestamp span (resolve + copies) costs 0.26-0.32 ms at 1080p and 0.46-0.54 ms at 1440p on the
benchmark laptop (`godot-4.7.2_profile`, day state), 3-12 % of the frame. Copies 2 and 3 are pure bandwidth
(≈ 2 × 16.6 MB + 8.3 MB moved at 1080p, ≈ 2 × 29.5 MB + 14.7 MB at 1440p).

## Stock implementation

`resolve(colour, temp, …, history, prev_velocity)`, then the three copies above.

## Proposed implementation (`TAA::process_pingpong`)

Two history textures and two previous-velocity textures per render buffer, selected by a parity bit stored in a
`RenderBufferCustomDataRD` (`FellowPingPongState`). The resolve reads history[read] / velocity[read] and writes the
new history directly to history[write]; a shader variant (`FELLOW_TAA_WRITE_VELOCITY`) also stores this frame's
velocity into velocity[write] from the same dispatch. Only one copy remains (history[write] → internal colour,
needed by the rest of the post chain). The first frame after (re)allocation seeds history and velocity like stock.
Works with the stock resolve and with resolve v2 (orthogonal toggles).

## Expected benefit

Two fewer full-screen copies: roughly 0.1 ms at 1080p and 0.15-0.2 ms at 1440p on this GPU (bandwidth estimate),
with **bit-identical output** (same arithmetic, different storage).

## Correctness check

Lavapipe (Linux, CPU Vulkan, deterministic): pbr_spheres captures with ping-pong on/off are bit-identical for both
the stock resolve and resolve v2. On the benchmark machine: `fellow-m1_capture` compares stock vs `fellow_pp` for
every fixed camera of every scene at 1080p and 1440p (expected: identical, see "Actual visual quality").

## Actual performance

Machine state for all numbers: RTX 5070 Ti Laptop at ~1.75 GHz, 70-77 °C, 71-82 W (day state, 2026-09-30), preset
`high`, stock and variants interleaved ABBA in one session.

**TAA pass (GPU timestamps, `fellow-m1_profile`)** — the span between the `TAA` and `Tonemap` markers, i.e. resolve
+ copies, one profiled run per configuration averaged over all frames of the flythrough and 300 frames per fixed
camera:

| Scene | Res | stock | ping-pong | Δ |
|---|---|---:|---:|---:|
| PBR Lab | 1920x1080 | 0.317 ms | 0.260 ms | −0.056 ms (−18 %) |
| PBR Lab | 2560x1440 | 0.454 ms | 0.373 ms | −0.081 ms (−18 %) |
| Sponza | 1920x1080 | 0.272 ms | 0.235 ms | −0.037 ms (−14 %) |
| Sponza | 2560x1440 | 0.537 ms | 0.419 ms | −0.118 ms (−22 %) |
| Bistro ext. | 1920x1080 | 0.260 ms | 0.215 ms | −0.045 ms (−17 %) |
| Bistro ext. | 2560x1440 | 0.481 ms | 0.380 ms | −0.101 ms (−21 %) |
| Bistro int. | 1920x1080 | 0.250 ms | 0.204 ms | −0.046 ms (−19 %) |
| Bistro int. | 2560x1440 | 0.464 ms | 0.380 ms | −0.084 ms (−18 %) |

Faster in 8/8 configurations; the saving scales with pixel count as expected for removed copies
(≈ 0.04-0.06 ms at 2.1 MPix, 0.08-0.12 ms at 3.7 MPix).

**Whole frame (`fellow-m1_perf`, 3-4 runs per configuration, `benchmarks/tools/ab_stats.py`):**

| Scene | Res | frame ms stock → ping-pong | Δ | p (Welch) |
|---|---|---:|---:|---:|
| PBR Lab | 1920x1080 | 2.978 → 2.943 | −0.034 (−1.2 %) | 0.14 |
| PBR Lab | 2560x1440 | 4.606 → 4.556 | −0.050 (−1.1 %) | 0.16 |
| Sponza | 1920x1080 | 13.468 → 13.203 | −0.264 (−2.0 %) | 0.12 |
| Sponza | 2560x1440 | 18.183 → 18.049 | −0.134 (−0.7 %) | 0.035 |
| Bistro ext. | 1920x1080 | 5.583 → 5.620 | +0.037 (+0.7 %) | 0.30 |
| Bistro ext. | 2560x1440 | 9.090 → 8.721 | −0.369 (−4.1 %) | 0.07 |
| Bistro int. | 1920x1080 | 4.307 → 4.265 | −0.042 (−1.0 %) | 0.005 |
| Bistro int. | 2560x1440 | 7.133 → 6.959 | −0.174 (−2.4 %) | 0.08 |

At frame level the saving is at the edge of what 3 runs can resolve: faster in 7/8 configurations, individually
significant only where the run-to-run spread is tiny (Bistro interior 1080p). The FellowEngine null test (all
toggles off) in the same session stays within ±0.2 ms / noise everywhere, so the frame-level numbers are consistent
with the pass-level saving but must not be quoted as more than "~1 %" — the pass timings are the evidence.

## Actual visual quality

* Lavapipe (deterministic CPU Vulkan): captures with ping-pong on/off are **bit-identical** for both resolves.
* RTX 5070 Ti (`fellow-m1_capture`, every fixed camera of every scene, 1080p + 1440p): ping-pong vs stock differs by
  max 1-22 / 255, mean ≤ 0.008 / 255 per pixel. **Stock vs stock** (two runs of the same binary, `godot-4.7.2_capture`
  vs `fellow-m1_capture`) differs by the same amount (max 1-11, mean ≤ 0.006), i.e. the NVIDIA path is not
  bit-deterministic run to run, and ping-pong is within that noise floor. No visual change.

## Regression risk

Extra VRAM: the pairs (2 × RGBA16F history + 2 × RG16F velocity) replace stock's temp + history (2 × RGBA16F) and
prev_velocity (1 × RG16F), i.e. **+1 RG16F texture per viewport** (8.3 MB at 1080p, 14.7 MB at 1440p). The stock
textures are not allocated while ping-pong is on (they are only created if the stock path runs). Multi-view (XR) paths use per-view slices like stock. The custom
data is dropped with the render buffers on resize, so history is re-seeded like the stock path.

## Decision

**KEEP.** Measured −14 % to −22 % of the TAA span in 8/8 configurations (−0.04 to −0.12 ms), no image change beyond the
GPU's own run-to-run noise, +1 RG16F texture per viewport. Remains behind `rendering/fellow/postfx/taa_history_pingpong`
(default off in the fork until the presets are defined; on in every FellowEngine configuration reported).
