# Immutable stock baseline — Godot 4.7.2-stable

Measured 2026-09-30 (day thermal state: RTX 5070 Ti Laptop ~1.75 GHz, 69-77 °C, 71-82 W) with the untouched
`4.7.2-stable` tag (branch `fellow/baseline`, commit `ed1daf0bf`), built with the flags in `BUILDING_FELLOWENGINE.md`.

| File | Content |
|---|---|
| `godot-4.7.2_perf.json/.csv` | performance, 4 scenes × 1080p/1440p × 3 runs (+ heat soak) |
| `godot-4.7.2_profile.json/.csv` | per-pass GPU timestamps (`--gpu-profile`), 1 run per configuration + 300 frames per fixed camera |
| `godot-4.7.2_noaa_profile.*` | same with preset `high_noaa` (TAA and motion vectors off) — cost of the TAA path |
| `godot-4.7.2_capture*.json`, `_temporal.json`, `_reference*.json` | image sessions (captures, temporal sequences, ground truth) |
| `MANIFEST.json` | sha256 of the stock binaries, of every session file and of every raw image (captures, EXRs, ground truth, Cycles export) |

The raw images live in `benchmarks/results/raw/` (not in git: > 1 GB); `MANIFEST.json` lets anyone verify that a
re-generated baseline is bit-identical where the GPU is deterministic.

Superseded / not part of the baseline (kept for transparency in `benchmarks/results/`):
* `godot-4.7.2-night45W_*` — first stock session, measured overnight in a thermally limited state (GPU ~1.0 GHz,
  87 °C, 45 W). PBR/Bistro are valid for that state; Sponza is invalid (textures imported uncompressed without
  mipmaps) and a later Sponza re-run was interrupted by the laptop going to sleep and had ~480 ms hitches.
* `diag_stall_*` — the four diagnostic runs used to look for the hitches (none reproduced in the day state).

Stock numbers in the milestone report's A/B tables come from `fellow-m1_*`, where stock was re-run interleaved with
the FellowEngine variants in the same session. **Stock differs between sessions** by up to 6 % (Bistro exterior
1080p: 5.96 ms here vs 5.58 ms in `fellow-m1_perf`; PBR Lab and Sponza within 2 %), i.e. more than the run-to-run
noise inside a session (CV ≤ 3 %). This is session-to-session drift of the laptop (GPU clocks 1.64-1.83 GHz between
runs), which is why every Stock-vs-FellowEngine comparison is made only *within* one interleaved session, never
between this baseline and a later session.
