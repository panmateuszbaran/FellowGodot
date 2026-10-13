# 0041 — SSAO and SSIL gather taps fetched a few at a time

Status: **REJECT** (option kept, off: 0) · Branch `fellow/geometry` · Toggle `rendering/fellow/postfx/ss_tap_batch`
(start-up, 0-8; 0 or 1 = stock's loop) · Code: `shaders/effects/ssao.glsl` (`SSAO_tap_math`, `SSAO_tap_coords`,
`SSAO_taps`), `shaders/effects/ssil.glsl` (`SSIL_tap_geometry`, `SSIL_tap_color`, `SSIL_tap_coords`, `SSIL_taps`),
`effects/ss_effects.cpp` (`FELLOW_SS_TAP_BATCH` as a general define of both gather shaders), `fellow_settings.*`.

## Idea

0040 found the scene shader's soft shadow loops waiting one fetch latency per tap. The SSAO and SSIL gathers have the
same shape: the tap count comes from a push constant (12 taps of two depth samples at the bench's SSAO quality, 5 at
its SSIL quality), so the loop is not unrolled, and each tap's samples feed the running sums before the next tap's
samples are fetched. SSAO's gather is the largest screen-space pass (0.34-0.44 ms at 1080p).

## Change

`SSAO_taps()` / `SSIL_taps()` replace the gathers' tap loops (the standard pass, the base pass of the adaptive
quality, and the temporal variants of 0028/0030; the adaptive pass's extra taps keep stock's loop). With the setting
at N ≥ 2, N taps' coordinates are computed, their depth samples (SSIL: and normals) fetched together, then (SSIL: the
geometry, the reprojected colour samples fetched together, then) every sum takes its terms in stock's order. The tap
code was split into functions that stock's path uses too (`SSAO_tap_inner` = fetch + `SSAO_tap_math`; `SSILTap` =
`SSIL_tap_coords` + two `SSIL_tap_inner`), so both paths run the same expressions. Taps left over (5 % 4) take stock's
loop.

## Measured (`fellow105`)

* Images: lavapipe, batches of 4 (and 3) against 0, SSIL switched on: City Lab, Lights Lab, PBR Lab captures, City Lab
  with temporal SSAO and SSIL — **0 differing pixels**. RTX sequences (`m8_f118b_ss_temporal`, batches of 4 against 0):
  Bistro exterior and City Lab bit for bit, the others at the floor of a null test of the same binary
  (`m8_f118_ss_temporal`: Bistro interior 67.1, Lights Lab 94.3, PBR Lab 76.3-76.4, Sponza 75.5 dB).
* Registers (`FELLOW_PIPELINE_STATS`, `m8_f118b_pstats_*`): SSAO gather 40 → 45 (batches of 2) → 64 (of 4); SSIL
  gather 53 → 80 → 128.
* Per pass (`m8_f118b_ss_passes`, 2 runs, async compute off):

| scene | Process SSAO: 0 → 2 → 4 | Process SSIL: 0 → 2 → 4 |
|---|---|---|
| Bistro exterior | 0.410 → 0.407 → 0.403 ms | 0.224 → 0.225 → 0.224 ms |
| Sponza | 0.440 → 0.385 → 0.383 | 0.216 → 0.218 → 0.224 |
| City Lab | 0.342 → 0.343 → 0.343 | |
| Lights Lab | 0.301 → 0.333 → 0.328 | |

* Frame time with the defaults (`m8_f118b_ss_perf`, async compute on, 3 runs, batches of 4 against 0): Bistro exterior
  +0.2 %, Bistro interior 0.0 %, City Lab +0.8 %, Sponza +1.1 %, PBR Lab −2.5 % (noisy), all within noise; Lights Lab
  **+1.3 %** (p 0.017).

(`m8_f118` meant to measure the same but set the option with `--fellow`, which the bench applies after start-up: its
"batched" runs ran stock's loop. Its sessions serve as null tests.)

## Why not

At 40 registers the SSAO gather keeps up to 48 warps per SM resident: while one warp waits for its samples, others
run, and the latency is hidden without batching. Batching only raises the register count (and the SSIL gather's to
128). The scene shader of 0040 runs at 80 registers with 24 warps per SM, where the waits showed. **REJECT**; the
option stays, off.
