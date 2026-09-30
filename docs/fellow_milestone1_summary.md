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
