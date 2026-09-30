# Stock Godot 4.7.2 vs FellowEngine — report 2026-09-30 (7f257b56ff)

## Milestone 1 — result in one paragraph

FellowEngine (Godot 4.7.2-stable + isolated, toggleable changes) was compared with untouched Godot 4.7.2 on four
benchmark scenes at 1920×1080 and 2560×1440, interleaved runs on one laptop (RTX 5070 Ti Laptop, day thermal state).
The first evidence-based experiment rewrote the TAA stage: **resolve v2** (reverse-Z closest-depth dilation,
dilated-velocity reprojection, YCoCg variance clip that does not collapse under motion) and **history ping-pong**
(two fewer full-screen copies). Result: the TAA stage is 3-15 % cheaper than stock in every configuration, whole-frame
time is unchanged within measurement noise, and the image is closer to an accumulated ground truth (2×2 SSAA × 8× MSAA × 8-16 jitters, TAA off) in
4/4 scenes on fixed cameras (LPIPS −19 to −25 %) and in 7 of 8 motion sequences. One measured regression: slightly more
shimmer on alpha-tested grass/fence during a lateral strafe. A GPU shallow-water prototype runs a 512² grid in 0.21 ms
(1024² in 0.75 ms) with no readback.

## The four questions

- **Does FellowEngine render better than stock Godot?** Yes, measurably but modestly, and only in the part that was
  changed (temporal anti-aliasing): closer to the ground truth in every scene on fixed cameras and in 7/8 motion
  sequences; worse in one (alpha-tested foliage strafe). Everything else renders identically to stock.
- **Does it run faster at equivalent quality?** Only the TAA stage: 0.04-0.12 ms faster with ping-pong at identical
  output (8/8 configurations, GPU timestamps). At whole-frame level this is ≤ ~1-4 % and not statistically resolvable
  with 3 runs per configuration. The big costs (directional shadows in Sponza, opaque shading everywhere) are untouched.
- **Does it produce better image quality at equivalent performance?** Yes: TAA v2 + ping-pong costs no more than stock
  TAA (TAA stage 3-15 % cheaper, frame time within noise) and improves SSIM/PSNR/LPIPS against the ground truth.
- **Can we prove those claims with reproducible measurements?** Yes for the claims above: deterministic frame
  sequences, interleaved runs with CV ≤ 3 %, Welch tests, per-pass GPU timestamps, lossless captures, accumulated
  ground truth and scripts to regenerate everything (`benchmarks/`). Limits: one machine, one GPU vendor, a laptop
  whose thermal state changed overnight (those runs were discarded, not mixed), and 3-4 runs per configuration.


## Summary (1920x1080, stock vs `fellow_v2`)

| Scene | Renderer | GPU ms | Render CPU ms | Frame ms | p95 ms | p99 ms | VRAM MB | SSIM vs reference | PSNR vs reference | LPIPS vs reference |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| PBR Validation Lab | Stock | 2.557 | 0.224 | 2.978 | 3.632 | 3.855 | 304 | 0.9860 | 38.70 | 0.0289 |
| PBR Validation Lab | FellowEngine | 2.636 | 0.224 | 3.045 | 3.695 | 3.886 | 313 | 0.9881 | 39.09 | 0.0219 |
| Intel Sponza | Stock | 13.082 | 0.654 | 13.468 | 19.543 | 20.873 | 2533 | 0.9821 | 40.68 | 0.0281 |
| Intel Sponza | FellowEngine | 12.718 | 0.673 | 13.103 | 18.634 | 19.478 | 2541 | 0.9840 | 41.06 | 0.0219 |
| Bistro Exterior | Stock | 5.214 | 1.457 | 5.583 | 6.193 | 6.291 | 2109 | 0.9719 | 34.37 | 0.0321 |
| Bistro Exterior | FellowEngine | 5.229 | 1.436 | 5.603 | 6.231 | 6.371 | 2118 | 0.9751 | 34.60 | 0.0252 |
| Bistro Interior | Stock | 3.957 | 0.593 | 4.307 | 4.733 | 4.824 | 1272 | 0.9860 | 38.23 | 0.0214 |
| Bistro Interior | FellowEngine | 3.948 | 0.589 | 4.307 | 4.732 | 4.830 | 1281 | 0.9875 | 38.48 | 0.0173 |

Image quality columns: mean over the scene's fixed cameras of the lossless capture vs. the accumulated ground truth (display-referred). Timing columns: interleaved ABBA runs, mean of per-run means.

## Stock vs FellowEngine (A/B, all variants)

| Scene | Res | Build | GPU ms | Frame ms | CPU ms | p95 ms | p99 ms | VRAM MB | CV | Runs |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| PBR Validation Lab | 1920x1080 | fellow_off [enabled=false] | 2.552 | 2.975 | 0.225 | 3.646 | 3.856 | 304 | 0.2% | 3 |
| PBR Validation Lab | 1920x1080 | fellow_pp [postfx/taa_history_pingpong=true] | 2.513 | 2.943 | 0.228 | 3.580 | 3.781 | 313 | 0.5% | 3 |
| PBR Validation Lab | 1920x1080 | fellow_v2 [postfx/taa_resolve_v2=true,postfx/taa_history_pingpong=true] | 2.636 | 3.045 | 0.224 | 3.695 | 3.886 | 313 | 3.0% | 3 |
| PBR Validation Lab | 1920x1080 | stock | 2.557 | 2.978 | 0.224 | 3.632 | 3.855 | 304 | 0.9% | 3 |
| PBR Validation Lab | 2560x1440 | fellow_off [enabled=false] | 4.202 | 4.597 | 0.232 | 5.517 | 5.759 | 426 | 0.9% | 3 |
| PBR Validation Lab | 2560x1440 | fellow_pp [postfx/taa_history_pingpong=true] | 4.157 | 4.556 | 0.235 | 5.521 | 5.781 | 441 | 0.8% | 3 |
| PBR Validation Lab | 2560x1440 | fellow_v2 [postfx/taa_resolve_v2=true,postfx/taa_history_pingpong=true] | 4.217 | 4.615 | 0.233 | 5.535 | 5.767 | 441 | 1.6% | 3 |
| PBR Validation Lab | 2560x1440 | stock | 4.214 | 4.606 | 0.233 | 5.551 | 5.729 | 426 | 0.8% | 3 |
| Intel Sponza | 1920x1080 | fellow_off [enabled=false] | 12.931 | 13.321 | 0.665 | 19.094 | 20.012 | 2533 | 2.5% | 3 |
| Intel Sponza | 1920x1080 | fellow_pp [postfx/taa_history_pingpong=true] | 12.814 | 13.203 | 0.680 | 18.901 | 20.073 | 2541 | 1.2% | 3 |
| Intel Sponza | 1920x1080 | fellow_v2 [postfx/taa_resolve_v2=true,postfx/taa_history_pingpong=true] | 12.718 | 13.103 | 0.673 | 18.634 | 19.478 | 2541 | 0.5% | 3 |
| Intel Sponza | 1920x1080 | stock | 13.082 | 13.468 | 0.654 | 19.543 | 20.873 | 2533 | 1.3% | 3 |
| Intel Sponza | 2560x1440 | fellow_off [enabled=false] | 17.725 | 18.156 | 0.712 | 25.157 | 26.263 | 2708 | 0.4% | 3 |
| Intel Sponza | 2560x1440 | fellow_pp [postfx/taa_history_pingpong=true] | 17.620 | 18.049 | 0.708 | 24.856 | 26.095 | 2723 | 0.2% | 3 |
| Intel Sponza | 2560x1440 | fellow_v2 [postfx/taa_resolve_v2=true,postfx/taa_history_pingpong=true] | 17.645 | 18.077 | 0.724 | 25.072 | 25.962 | 2723 | 0.4% | 3 |
| Intel Sponza | 2560x1440 | stock | 17.754 | 18.183 | 0.732 | 25.329 | 26.558 | 2708 | 0.3% | 3 |
| Bistro Exterior | 1920x1080 | fellow_off [enabled=false] | 5.190 | 5.561 | 1.454 | 6.194 | 6.283 | 2109 | 0.9% | 3 |
| Bistro Exterior | 1920x1080 | fellow_pp [postfx/taa_history_pingpong=true] | 5.250 | 5.620 | 1.439 | 6.252 | 6.351 | 2118 | 0.8% | 3 |
| Bistro Exterior | 1920x1080 | fellow_v2 [postfx/taa_resolve_v2=true,postfx/taa_history_pingpong=true] | 5.229 | 5.603 | 1.436 | 6.231 | 6.371 | 2118 | 0.5% | 3 |
| Bistro Exterior | 1920x1080 | stock | 5.214 | 5.583 | 1.457 | 6.193 | 6.291 | 2109 | 0.3% | 3 |
| Bistro Exterior | 2560x1440 | fellow_off [enabled=false] | 8.496 | 8.914 | 1.638 | 9.862 | 10.043 | 2285 | 1.4% | 4 |
| Bistro Exterior | 2560x1440 | fellow_pp [postfx/taa_history_pingpong=true] | 8.303 | 8.721 | 1.636 | 9.708 | 9.904 | 2300 | 2.4% | 4 |
| Bistro Exterior | 2560x1440 | fellow_v2 [postfx/taa_resolve_v2=true,postfx/taa_history_pingpong=true] | 8.326 | 8.751 | 1.619 | 9.706 | 9.923 | 2300 | 0.7% | 4 |
| Bistro Exterior | 2560x1440 | stock | 8.668 | 9.090 | 1.626 | 10.224 | 10.549 | 2285 | 2.8% | 4 |
| Bistro Interior | 1920x1080 | fellow_off [enabled=false] | 3.954 | 4.306 | 0.594 | 4.735 | 4.829 | 1272 | 0.1% | 3 |
| Bistro Interior | 1920x1080 | fellow_pp [postfx/taa_history_pingpong=true] | 3.914 | 4.265 | 0.598 | 4.688 | 4.782 | 1281 | 0.1% | 3 |
| Bistro Interior | 1920x1080 | fellow_v2 [postfx/taa_resolve_v2=true,postfx/taa_history_pingpong=true] | 3.948 | 4.307 | 0.589 | 4.732 | 4.830 | 1281 | 0.8% | 3 |
| Bistro Interior | 1920x1080 | stock | 3.957 | 4.307 | 0.593 | 4.733 | 4.824 | 1272 | 0.2% | 3 |
| Bistro Interior | 2560x1440 | fellow_off [enabled=false] | 6.728 | 7.136 | 0.679 | 7.706 | 7.948 | 1448 | 0.5% | 3 |
| Bistro Interior | 2560x1440 | fellow_pp [postfx/taa_history_pingpong=true] | 6.545 | 6.959 | 0.677 | 7.518 | 7.741 | 1463 | 1.5% | 3 |
| Bistro Interior | 2560x1440 | fellow_v2 [postfx/taa_resolve_v2=true,postfx/taa_history_pingpong=true] | 6.643 | 7.058 | 0.680 | 7.637 | 7.885 | 1463 | 1.2% | 3 |
| Bistro Interior | 2560x1440 | stock | 6.726 | 7.133 | 0.688 | 7.714 | 7.945 | 1448 | 0.5% | 3 |

## Immutable stock baseline

| Scene | Res | Build | GPU ms | Frame ms | CPU ms | p95 ms | p99 ms | VRAM MB | CV | Runs |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| PBR Validation Lab | 1920x1080 | stock | 2.530 | 2.957 | 0.229 | 3.570 | 3.748 | 304 | 1.2% | 3 |
| PBR Validation Lab | 2560x1440 | stock | 4.188 | 4.580 | 0.233 | 5.553 | 5.766 | 426 | 0.8% | 3 |
| Intel Sponza | 1920x1080 | stock | 13.321 | 13.712 | 0.628 | 19.916 | 20.785 | 2533 | 0.9% | 3 |
| Intel Sponza | 2560x1440 | stock | 17.889 | 18.315 | 0.648 | 25.275 | 26.352 | 2708 | 0.1% | 3 |
| Bistro Exterior | 1920x1080 | stock | 5.579 | 5.962 | 1.433 | 6.692 | 6.849 | 2109 | 1.8% | 3 |
| Bistro Exterior | 2560x1440 | stock | 8.709 | 9.139 | 1.575 | 10.215 | 10.462 | 2285 | 1.5% | 3 |
| Bistro Interior | 1920x1080 | stock | 4.161 | 4.529 | 0.587 | 4.997 | 5.200 | 1272 | 1.2% | 3 |
| Bistro Interior | 2560x1440 | stock | 6.968 | 7.379 | 0.660 | 8.009 | 8.249 | 1448 | 0.0% | 3 |

## Fluid Lab

See index.html (fluid section).

Hardware: see hardware.json. Settings: see settings.json.
